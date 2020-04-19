#include "supervisor_connection.h"
#include <utility>
#include <boost/move/adl_move_swap.hpp>
#include <spdlog/spdlog.h>
#include "simple_worker_proto.h"

namespace vNerve::bilibili::worker_supervisor
{

supervisor_connection::supervisor_connection(const config::config_t config,
                                             const supervisor_buffer_handler buffer_handler,
    const supervisor_connected_handler connected_handler)
    : _config(config),
      _guard(_context.get_executor()),
      _resolver(_context),
      _read_buffer_size((*config)["read-buffer"].as<size_t>()),
      _timer(_context),
      _retry_interval_sec((*config)["retry-interval-sec"].as<int>()),
      _supervisor_host((*config)["supervisor-host"].as<std::string>()),
      _supervisor_port(std::to_string((*config)["supervisor-port"].as<int>())),
      _buffer_handler(buffer_handler),
      _connected_handler(connected_handler)
{
    _read_buffer_ptr =
        std::unique_ptr<unsigned char[]>(new unsigned char[_read_buffer_size]);
    _thread = boost::thread(boost::bind(&boost::asio::io_context::run, &_context));
    _context.post(boost::bind(connect, shared_from_this()));
}

supervisor_connection::~supervisor_connection()
{
    _guard.reset();
    _context.stop();
    // todo exception handling
}

void supervisor_connection::publish_msg(unsigned char* msg, size_t len,
                                        supervisor_buffer_deleter deleter)
{
    if (!_socket)
    {
        deleter(msg); // Dispose data.
        return;
    }
    bool ret = _queue.enqueue(std::make_pair(boost::asio::buffer(msg, len), deleter));
    if (!ret)
    {
        deleter(msg);
        return;
    }
    post(_context,
         boost::bind(&supervisor_connection::start_async_write,
                     shared_from_this()));
}

void supervisor_connection::start_async_write()
{
    if (!_socket || _pending_write)
        return;
    _pending_write = true;

    std::array<supervisor_buffer_owned, MAX_WRITE_BATCH> bufs;
    auto bufCount = _queue.try_dequeue_bulk(bufs.data(), MAX_WRITE_BATCH);
    if (bufCount == 0)
    {
        _pending_write = false;
        return;
    }

    auto bufs2 = std::vector<boost::asio::const_buffer>(bufCount);
    for (size_t i = 0; i < bufCount; i++)
        bufs2[i] = bufs[i].first;

    async_write(
        *_socket, bufs2,
        boost::bind(&supervisor_connection::on_written, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred,
                    bufs, bufCount));
}

void supervisor_connection::start_async_read()
{
    // TODO log
    if (!_socket)
        return;
    spdlog::trace(
        "[supervisor_conn] Starting next async read. offset={}, size={}/{}", _read_buffer_offset, _read_buffer_size - _read_buffer_offset, _read_buffer_size);
    _socket->async_receive(
        boost::asio::buffer(_read_buffer_ptr.get() + _read_buffer_offset,
                            _read_buffer_size - _read_buffer_offset),
        boost::bind(on_receive, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void supervisor_connection::connect()
{
    if (_socket)
        force_close();

    _resolver.async_resolve(
        _supervisor_host, _supervisor_port,
        boost::bind(&supervisor_connection::on_resolved, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::iterator));
}

void supervisor_connection::force_close()
{
    auto nec = boost::system::error_code();
    _socket->shutdown(boost::asio::socket_base::shutdown_both, nec);
    _socket->close(nec);  // nec ignored
    _socket.reset();
}

void supervisor_connection::reschedule_retry_timer()
{
    _timer.expires_from_now(boost::posix_time::seconds(_retry_interval_sec));
    _timer.async_wait(boost::bind(&supervisor_connection::on_retry_timer_tick, shared_from_this(), boost::asio::placeholders::error));
}

void supervisor_connection::on_retry_timer_tick(const boost::system::error_code& ec)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[supervisor_conn] Cancelling retrying timer.");
            return;  // closing socket.
        }
        spdlog::warn("[supervisor_conn] Error in retrying timer!", ec.value(), ec.message());
        // Don't exit
    }

    connect();
}

void supervisor_connection::on_written(const boost::system::error_code& ec,
                                       size_t bytes_transferred, std::array<supervisor_buffer_owned, MAX_WRITE_BATCH> buffers, size_t batch_size)
{
    // First delete all buffers and the vector itself!!
    for (size_t i = 0; i < batch_size;i++)
        buffers[i].second(reinterpret_cast<unsigned char*>(buffers[i].first.data()));

    _pending_write = false;

    if (ec)
    {
        // TODO error handling and closing socket!
        // TODO retry handling?
    }
    // todo log?

    start_async_write();
}

void supervisor_connection::on_resolved(const boost::system::error_code& ec,
                                        const boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
    if (ec)
    {
        // TODO error handling
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[supervisor_conn] Cancelling connecting to supervisor.");
            return;
        }
        spdlog::warn(
            "[supervisor_conn] Failed resolving DN to supervisor! err: {}:{}", ec.value(), ec.message());
        reschedule_retry_timer();
        return;
    }
    spdlog::debug(
        "[supervisor_conn] Connecting to supervisor {}: server DN resolved, connecting to endpoints.");

    boost::system::error_code nec;
    _timer.cancel(nec);
    // TODO notification?
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    async_connect(
        *socket, endpoint_iterator,
        boost::bind(&supervisor_connection::on_connected, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::iterator, socket));
}

void supervisor_connection::on_connected(const boost::system::error_code& ec, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (ec)
    {
        // TODO error handling
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[supervisor_conn] Cancelling connecting to supervisor.");
            return;
        }
        spdlog::warn(
            "[supervisor_conn] Failed connecting to supervisor! err: {}:{}", ec.value(), ec.message());

        reschedule_retry_timer();
        return;
    }

    boost::system::error_code nec;
    _timer.cancel(nec);
    if (_socket)
        force_close();
    _socket = socket;

    // todo logging
    start_async_read();
    _connected_handler();
}

void supervisor_connection::on_receive(const boost::system::error_code& ec, size_t transferred)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[supervisor_conn] Cancelling async reading.", ec);
            return;  // closing socket.
        }
        // TODO error handling
    }

    spdlog::debug("[supervisor_conn] Received data block(len={})", transferred);
    auto [new_offset, new_skipping_bytes] =
        handle_simple_message(_read_buffer_ptr.get(), transferred, _read_buffer_size,
                              _skipping_bytes, _buffer_handler);
    _read_buffer_offset = new_offset;
    _skipping_bytes = new_skipping_bytes;

    start_async_read();
}
}
