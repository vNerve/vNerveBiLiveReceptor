#include "supervisor_connection.h"
#include <spdlog/spdlog.h>

namespace vNerve::bilibili::worker_supervisor
{

supervisor_connection::supervisor_connection(const config::config_t config,
                                             const supervisor_buffer_handler buffer_handler,
    supervisor_connected_handler connected_handler,
    supervisor_connected_handler disconnected_handler)
    : _config(config),
      _guard(_context.get_executor()),
      _resolver(_context),
      _proto_handler(std::make_shared<simple_worker_proto_handler>("[sv_conn]", nullptr, ((*config)["read-buffer"].as<size_t>()), buffer_handler, boost::bind(&supervisor_connection::on_failed, this))),
      _write_helper(std::make_shared<asio_socket_write_helper>("[sv_conn]", nullptr, boost::bind(&supervisor_connection::on_failed, this))),
      _timer(_context),
      _retry_interval_sec((*config)["retry-interval-sec"].as<int>()),
      _supervisor_host((*config)["supervisor-host"].as<std::string>()),
      _supervisor_port(std::to_string((*config)["supervisor-port"].as<int>())),
      _connected_handler(std::move(connected_handler)),
      _disconnected_handler(std::move(disconnected_handler))
{
    _thread = boost::thread(boost::bind(&boost::asio::io_context::run, &_context));
    _timer.expires_from_now(boost::posix_time::seconds(5));
    _timer.async_wait(boost::bind(&supervisor_connection::on_retry_timer_tick, this, boost::asio::placeholders::error));
}

supervisor_connection::~supervisor_connection()
{
    try
    {
        force_close();
        _guard.reset();
        _context.stop();
    }
    catch (boost::system::system_error& ex)
    {
        spdlog::critical("[sv_conn] Failed shutting down session IO Context! err:{}:{}:{}",
                         ex.code().value(), ex.code().message(), ex.what());
    }

}

void supervisor_connection::publish_msg(unsigned char* msg, size_t len,
                                        supervisor_buffer_deleter deleter)
{
    if (!_socket)
    {
        deleter(msg); // Dispose data.
        return;
    }
    _write_helper->write(msg, len, deleter);
}

void supervisor_connection::join()
{
    _thread.join();
}

void supervisor_connection::connect()
{
    if (_socket)
        force_close();

    _resolver.async_resolve(
        _supervisor_host, _supervisor_port,
        boost::bind(&supervisor_connection::on_resolved, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::iterator));
}

void supervisor_connection::force_close()
{
    auto nec = boost::system::error_code();
    if (!_socket)
        return;
    _socket->shutdown(boost::asio::socket_base::shutdown_both, nec);
    _socket->close(nec);  // nec ignored
    _socket.reset();
}

void supervisor_connection::reschedule_retry_timer()
{
    _timer.cancel();
    _timer.expires_from_now(boost::posix_time::seconds(_retry_interval_sec));
    _timer.async_wait(boost::bind(&supervisor_connection::on_retry_timer_tick, this, boost::asio::placeholders::error));
}

void supervisor_connection::on_retry_timer_tick(const boost::system::error_code& ec)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[sv_conn] Cancelling retrying timer.");
            return;  // closing socket.
        }
        spdlog::warn("[sv_conn] Error in retrying timer!", ec.value(), ec.message());
        // Don't exit
    }

    connect();
}

void supervisor_connection::on_resolved(const boost::system::error_code& ec,
                                        const boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[sv_conn] Cancelling connecting to supervisor.");
            return;
        }
        spdlog::warn(
            "[sv_conn] Failed resolving DN to supervisor! err: {}:{}", ec.value(), ec.message());
        reschedule_retry_timer();
        return;
    }
    spdlog::debug(
        "[sv_conn] Connecting to supervisor {}: server DN resolved, connecting to endpoints.");

    boost::system::error_code nec;
    _timer.cancel(nec);
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    async_connect(
        *socket, endpoint_iterator,
        boost::bind(&supervisor_connection::on_connected, this,
                    boost::asio::placeholders::error,
                    socket));
}

void supervisor_connection::on_connected(const boost::system::error_code& ec, std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[sv_conn] Cancelling connecting to supervisor.");
            return;
        }
        spdlog::warn(
            "[sv_conn] Failed connecting to supervisor! err: {}:{}", ec.value(), ec.message());

        reschedule_retry_timer();
        return;
    }

    boost::system::error_code nec;
    _timer.cancel(nec);
    if (_socket)
        force_close();
    _socket = socket;

    spdlog::info("[sv_conn] Connecting to server.");
    _proto_handler->reset(socket);
    _write_helper->reset(socket);
    _connected_handler();
}

void supervisor_connection::on_failed()
{
    force_close();
    _disconnected_handler();
    reschedule_retry_timer();
}
}
