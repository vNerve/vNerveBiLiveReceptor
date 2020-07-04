#include "worker_connection_manager.h"

#include <spdlog/spdlog.h>
#include <utility>

#define LOG_PREFIX "[sv_session] "

namespace vNerve::bilibili::worker_supervisor
{
worker_session::worker_session(
    identifier_t identifier,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    size_t read_buffer_size,
    supervisor_buffer_handler buffer_handler,
    supervisor_worker_disconnect_handler disconnect_handler)
    : _identifier(identifier),
      _socket(socket),
      _buffer_handler(std::move(buffer_handler)),
      _disconnect_handler(std::move(disconnect_handler)),
      _read_buffer_size(read_buffer_size)
{

}

worker_session::~worker_session()
{
    if (_socket)
        disconnect(false);
}

void worker_session::init()
{
    _write_helper = std::make_shared<asio_socket_write_helper>(
        fmt::format(LOG_PREFIX "[{:016x}]", _identifier),
        _socket,
        std::bind(&worker_session::disconnect, shared_from_this(), true));
    _read_handler = std::make_shared<simple_worker_proto_handler>(
        fmt::format(LOG_PREFIX "[{:016x}]", _identifier),
        _socket, _read_buffer_size,
        std::bind(_buffer_handler, _identifier, std::placeholders::_1, std::placeholders::_2),
        std::bind(&worker_session::disconnect, shared_from_this(), true));
    _read_handler->start();
}

void worker_session::send(unsigned char* buf, size_t len, supervisor_buffer_deleter deleter)
{
    _write_helper->write(buf, len, deleter);
}

void worker_session::disconnect(bool callback)
{
    if (_socket)
        return;
    spdlog::info(LOG_PREFIX "[{:016x}] Disconnecting worker socket.", _identifier);
    auto ec = boost::system::error_code();
    _socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    _socket->close(ec);

    _socket.reset();

    if (ec)
        spdlog::warn(LOG_PREFIX "[{:016x}] Error closing socket! err:{}:{}", _identifier, ec.value(), ec.message());
    if (callback)
        _disconnect_handler(_identifier);
}

worker_connection_manager::worker_connection_manager(
    const config::config_sv_t config,
    supervisor_buffer_handler buffer_handler,
    supervisor_tick_handler tick_handler,
    supervisor_new_worker_handler new_worker_handler,
    supervisor_worker_disconnect_handler disconnect_handler)
    : _config(config),
      _guard(_context.get_executor()),
      _acceptor(_context, boost::asio::ip::tcp::v4()),
      _timer(std::make_unique<boost::asio::deadline_timer>(_context)),
      _buffer_handler(std::move(buffer_handler)),
      _tick_handler(std::move(tick_handler)),
      _new_worker_handler(std::move(new_worker_handler)),
      _disconnect_handler(std::move(disconnect_handler)),
      _rand_engine(_rand())
{
    _thread =
        boost::thread(boost::bind(&boost::asio::io_context::run, &_context));

    auto port = config->worker.port;
    try
    {
        _acceptor.bind(boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port));
        _acceptor.listen();
    }
    catch (boost::system::system_error& ex)
    {
        spdlog::critical(LOG_PREFIX "Failed to listen on port {}! err:{}:{}", port, ex.code().value(), ex.code().message());
        throw;
    }

    start_accept();
    reschedule_timer();
}

worker_connection_manager::
~worker_connection_manager()
{
    try
    {
        boost::system::error_code nec;
        _acceptor.cancel(nec);
        _acceptor.close(nec);
        _timer->cancel(nec);
        _guard.reset();
        _context.stop();
    }
    catch (boost::system::system_error& ex)
    {
        spdlog::critical(LOG_PREFIX "Failed shutting down session IO Context! err:{}:{}:{}",
                         ex.code().value(), ex.code().message(), ex.what());
    }
}

void worker_connection_manager::
start_accept()
{
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    _acceptor.async_accept(
        *socket,
        boost::bind(&worker_connection_manager::on_accept, this,
                    boost::asio::placeholders::error, socket));
}

void worker_connection_manager::join()
{
    _thread.join();
}

void worker_connection_manager::on_accept(
    const boost::system::error_code& err,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (err)
    {
        if (err.value() == boost::system::errc::operation_canceled)
        {
            SPDLOG_DEBUG(LOG_PREFIX "Cancelling server accepting.");
            return;
        }
        spdlog::error(LOG_PREFIX "Error accepting worker socket! err:{}:{}", err.value(), err.message());
    }
    else
    {
        identifier_t identifier = _rand_dist(_rand_engine);
        while (_sockets.find(identifier) != _sockets.end())
            identifier = _rand_dist(_rand_engine);
        boost::system::error_code nec;
        auto remote_ep = socket->remote_endpoint(nec);
        if (nec)
        {
            spdlog::info(LOG_PREFIX "Invalid new worker connection: Couldn't get remote endpoint! Disconnecting");
            socket->close(nec);
            return;
        }
        spdlog::info(LOG_PREFIX "Accepting worker connection from {}:{}, associating id {:016x}.",
            remote_ep.address().to_string(),
            remote_ep.port(),
            identifier);
        auto [iter, inserted] = _sockets.emplace(
            identifier,
            std::make_shared<worker_session>(
                identifier, socket, _config->worker.read_buffer_size, _buffer_handler, _disconnect_handler));
        iter->second->init();

        _new_worker_handler(identifier);
    }

    start_accept();
}

void worker_connection_manager::reschedule_timer()
{
    _timer->expires_from_now(
        boost::posix_time::milliseconds(_config->worker.min_check_interval_msec));
    _timer->async_wait(boost::bind(&worker_connection_manager::on_timer_tick,
                                   this,
                                   boost::asio::placeholders::error));
}

void worker_connection_manager::on_timer_tick(const boost::system::error_code& ec)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug(LOG_PREFIX "Cancelling server tick.");
            return; // closing socket.
        }
        spdlog::error(LOG_PREFIX "Error in server tick timer! err:{}:{}", ec.value(), ec.message());
    }

    _tick_handler();
    reschedule_timer();
}

void worker_connection_manager::
    send_message(identifier_t identifier, unsigned char* msg, size_t len, supervisor_buffer_deleter deleter)
{
    auto socket_iter = _sockets.find(identifier);
    if (socket_iter == _sockets.end())
        return;

    socket_iter->second->send(msg, len, deleter);
}

void worker_connection_manager::disconnect_worker(identifier_t identifier, bool callback)
{
    auto socket_iter = _sockets.find(identifier);
    if (socket_iter == _sockets.end())
        return;

    std::shared_ptr<worker_session> session = socket_iter->second;
    session->disconnect(callback);
    _sockets.erase(socket_iter);
}
}
