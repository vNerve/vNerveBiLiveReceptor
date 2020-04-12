#include "supervisor_connection.h"
#include <utility>
#include <boost/move/adl_move_swap.hpp>

namespace vNerve::bilibili::worker_supervisor
{

supervisor_connection::supervisor_connection(const config::config_t config,
                                             const supervisor_buffer_handler buffer_handler)
    : _config(config),
      _guard(_context.get_executor()),
      _resolver(_context),
      _timer(_context),
      _retry_interval_sec((*config)["retry-interval-sec"].as<int>()),
      _supervisor_host((*config)["supervisor-host"].as<std::string>()),
      _supervisor_port(std::to_string((*config)["supervisor-port"].as<int>())),
      _buffer_handler(buffer_handler)
{
    _thread = boost::thread(boost::bind(&boost::asio::io_context::run, &_context));
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

void supervisor_connection::connect()
{
    if (_socket)
    {
        // TODO exception handling
        _socket->close();
        _socket.reset();
    }
    _socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    _resolver.async_resolve(
        _supervisor_host, _supervisor_port,
        boost::bind(&supervisor_connection::on_resolved, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::iterator));
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
        _socket->close();
        _socket.reset();
        // TODO retry handling?
    }
    // todo log?

    start_async_write();
}

void supervisor_connection::on_resolved(const boost::system::error_code& ec,
                                        const boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
}
}
