#include "amqp_client.h"

#include <memory>
#include <boost/thread.hpp>

namespace vNerve::bilibili::mq
{
amqp_asio_connection::amqp_asio_connection(const std::string& host, int port, const AMQP::Login& login, const std::string& vhost)
    : _login(login),
      _vhost(vhost),
      _guard(_context.get_executor()),
      _resolver(_context.get_executor()),
      _timer(_context.get_executor()),
      _host(host),
      _port(port)
{
    _thread = boost::thread(boost::bind(&boost::asio::io_context::run, &_context));
    _read_buffer = new unsigned char[READ_BUFFER_LEN];
    _read_buffer_guard = std::unique_ptr<unsigned char[]>(_read_buffer);
}

void amqp_asio_connection::post(std::function<void()> func)
{
    boost::asio::post(_context.get_executor(), func);
}

void amqp_asio_connection::onData(AMQP::Connection* connection, const char* buffer, size_t size)
{
    if (connection != _connection && !_initializing || !_socket)
        return;

    boost::system::error_code ec;
    _socket->send(boost::asio::buffer(buffer, size), 0, ec);

    if (ec)
    {
        // TODO log
        connection->fail(ec.message().c_str());
        close_socket();
    }
}

void amqp_asio_connection::onError(AMQP::Connection* connection, const char* message)
{
    // TODO log
    if (connection != _connection && !_initializing || !_socket)
        return;
    _initializing = false;
    close_socket();
}

void amqp_asio_connection::onReady(AMQP::Connection* connection)
{
    _initializing = false;
    start_heartbeat_timer();
    _onReady();
}

void amqp_asio_connection::onClosed(AMQP::Connection* connection)
{
    if (connection != _connection && !_initializing || !_socket)
        return;
    _initializing = false;
    close_socket();
}

uint16_t amqp_asio_connection::onNegotiate(AMQP::Connection* connection, uint16_t interval)
{
    if (connection != _connection && !_initializing || !_socket)
        return interval;
    _heartbeat_interval_sec = std::min(static_cast<int>(interval), MIN_HEARTBEAT_LEN_SEC) / 2;
    return _heartbeat_interval_sec * 2;
}

bool amqp_asio_connection::reconnect(std::function<void()> onReady)
{
    if (_connection)
    {
        _connection->close();
        delete _connection;
        _connection = nullptr;
    }
    _socket = nullptr;

    boost::system::error_code ec;
    auto endpoints = _resolver.resolve(_host, std::to_string(_port), ec);
    if (ec)
    {
        // TODO log
        return false;
    }
    if (_socket && _connection)
        return false;
    _socket = std::make_shared<boost::asio::ip::tcp::socket>(_context.get_executor());
    boost::asio::connect(*_socket, endpoints, ec);
    if (ec)
    {
        // TODO log
        return false;
    }

    _initializing = true;
    _onReady = onReady;
    _connection = new AMQP::Connection(this, _login, _vhost);

    start_async_read();
    return true;
}

void amqp_asio_connection::close_socket()
{
    _buffer_last_remaining = 0;
    if (!_socket)
        return;
    _socket->shutdown(boost::asio::socket_base::shutdown_both);

    boost::system::error_code ec = boost::system::error_code(boost::system::errc::operation_canceled, boost::system::system_category());
    _timer.cancel(ec);
    _socket->close(ec);
    _socket = nullptr;
}

void amqp_asio_connection::start_async_read()
{
    if (!_socket)
        return;
    _socket->async_receive(boost::asio::buffer(_read_buffer + _buffer_last_remaining, READ_BUFFER_LEN - _buffer_last_remaining),
                           boost::bind(&amqp_asio_connection::on_received, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void amqp_asio_connection::start_heartbeat_timer()
{
    boost::system::error_code ec;
    _timer.cancel(ec);
    _timer.expires_from_now(boost::posix_time::seconds(_heartbeat_interval_sec));
    _timer.async_wait(boost::bind(&amqp_asio_connection::on_timer_tick, this, boost::asio::placeholders::error));
}

void amqp_asio_connection::on_timer_tick(const boost::system::error_code& ec)
{
    if (ec)
        return;
    if (_connection && _connection->usable())
        _connection->heartbeat();

    start_heartbeat_timer();
}

void amqp_asio_connection::on_received(const boost::system::error_code& ec, size_t transferred)
{
    if (ec)
    {
        if (_connection)
            _connection->fail(ec.message().c_str());
        return;
        // TODO log
    }
    if (!_connection)
        return;
    auto ptr = _read_buffer;
    auto remaining = transferred + _buffer_last_remaining;
    size_t readLen;

    do
    {
        readLen = _connection->parse(reinterpret_cast<char*>(ptr), remaining);
        remaining -= readLen;
        ptr += readLen;
    } while (readLen > 0 && remaining > 0);

    if (remaining > 0)
    {
        std::memmove(_read_buffer, ptr, remaining);
        _buffer_last_remaining = remaining;
    }

    start_async_read();
}
}