#include "supervisor_server_session.h"

namespace vNerve::bilibili::worker_supervisor
{
supervisor_server_session::
supervisor_server_session(
    const config::config_t config,
    const supervisor_buffer_handler buffer_handler,
    const supervisor_tick_handler tick_handler,
    const supervisor_new_worker_handler new_worker_handler,
    const supervisor_worker_disconnect_handler disconnect_handler)
    : _guard(_context.get_executor()),
      _config(config),
      _buffer_handler(buffer_handler),
      _tick_handler(tick_handler),
      _new_worker_handler(new_worker_handler),
      _disconnect_handler(disconnect_handler),
      _timer(std::make_unique<boost::asio::deadline_timer>(_context)),
      _acceptor(_context),
      _rand_engine(_rand()),
      _timer_interval_ms((*config)["check-interval-ms"].as<int>()),
      _read_buffer_size((*config)["read-buffer"].as<size_t>())
{
    _thread =
        boost::thread(boost::bind(&boost::asio::io_context::run, &_context));

    start_accept();
    reschedule_timer();
}

supervisor_server_session::
~supervisor_server_session()
{
    _context.stop();
    // TODO gracefully
}

void supervisor_server_session::
start_accept()
{
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    _acceptor.async_accept(
        *socket,
        boost::bind(&supervisor_server_session::on_accept, shared_from_this(),
                    boost::asio::placeholders::error, socket));
}

void supervisor_server_session::on_accept(
    const boost::system::error_code& err,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    if (err)
    {
        // todo log
        // NOTE: Don't return from this block!
    }
    else
    {
        identifier_t identifier = _rand_dist(_rand_engine);
        _sockets.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(identifier),
            std::forward_as_tuple(
                socket,
                std::make_unique<simple_worker_proto_handler>(
                    socket, _read_buffer_size,
                    std::bind(_buffer_handler, identifier, std::placeholders::_1, std::placeholders::_2),
                    std::bind(&supervisor_server_session::disconnect_worker, shared_from_this(), identifier, true))));
        _new_worker_handler(identifier);
    }

    start_accept();
}

void supervisor_server_session::reschedule_timer()
{
    _timer->expires_from_now(
        boost::posix_time::milliseconds(_timer_interval_ms));
    _timer->async_wait(boost::bind(&supervisor_server_session::on_timer_tick,
                                   shared_from_this(),
                                   boost::asio::placeholders::error));
}

void supervisor_server_session::on_timer_tick(const boost::system::error_code& ec)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
        {
            return; // closing socket.
        }
        // TODO logging
    }

    _tick_handler();

    reschedule_timer();
}

void supervisor_server_session::
    send_message(identifier_t identifier, unsigned char* msg, size_t len, supervisor_buffer_deleter deleter)
{
    auto socket_iter = _sockets.find(identifier);
    if (socket_iter == _sockets.end())
        return;

    auto& socket = *(socket_iter->second.first);
    // TODO use a queue!!!!
    boost::asio::async_write(socket, boost::asio::const_buffer(msg, len),
                             [this, msg, deleter, identifier](const boost::system::error_code& ec,
                                   std::size_t) -> void
                             {
                                 // todo log error here?
                                 deleter(msg);
                                 if (ec.value() == boost::asio::error::operation_aborted)
                                     return;
                                 if (ec)
                                    disconnect_worker(identifier, true);
                             });
}

void supervisor_server_session::disconnect_worker(identifier_t identifier, bool callback)
{
    auto socket_iter = _sockets.find(identifier);
    if (socket_iter == _sockets.end())
        return;

    auto socket = socket_iter->second.first;
    _sockets.erase(socket_iter);
    auto ec = boost::system::error_code();
    socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    socket->close(ec);

    if (ec)
    {
        // log
    }
    if (callback)
        _disconnect_handler(identifier);
}
}
