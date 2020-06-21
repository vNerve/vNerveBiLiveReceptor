#include "amqp_client.h"

#include <memory>
#include <boost/thread.hpp>
#include <utility>
#include <spdlog/spdlog.h>

namespace vNerve::bilibili::mq
{
amqp_asio_connection::amqp_asio_connection(const std::string& host, int port, const AMQP::Login& login, const std::string& vhost, int reconnect_interval_sec)
    : _login(login),
      _vhost(vhost),
      _reconnect_interval_sec(reconnect_interval_sec),
      _guard(_context.get_executor()),
      _resolver(_context.get_executor()),
      _timer(_context.get_executor()),
      _reconnect_timer(_context.get_executor()),
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
    if (((connection != _connection) && !_initializing) || !_socket)
        return;

    SPDLOG_TRACE("[amqp] Sending data. len={}", size);
    boost::system::error_code ec;
    _socket->send(boost::asio::buffer(buffer, size), 0, ec);

    if (ec)
    {
        auto msg = ec.message();
        spdlog::error("[amqp] Failed sending data to AMQP broker! Disconnecting. err:{}", ec.value(), msg);
        connection->fail(msg.c_str());
        close_socket();
    }
}

void amqp_asio_connection::onError(AMQP::Connection* connection, const char* message)
{
    if (((connection != _connection) && !_initializing) || !_socket)
        return;
    spdlog::error("[amqp] Error handling data from AMQP broker! Disconnecting err:{}", message);
    _initializing = false;
    close_socket();
}

void amqp_asio_connection::onReady(AMQP::Connection* connection)
{
    _initializing = false;
    start_heartbeat_timer();
    spdlog::info("[amqp] AMQP broker connecting ready.");
    _onReady();
}

void amqp_asio_connection::onClosed(AMQP::Connection* connection)
{
    if (((connection != _connection) && !_initializing) || !_socket)
        return;
    _initializing = false;
    spdlog::info("[amqp] AMQP broker disconnecting.");
    close_socket(true);
}

uint16_t amqp_asio_connection::onNegotiate(AMQP::Connection* connection, uint16_t interval)
{
    if (((connection != _connection) && !_initializing) || !_socket)
        return interval;
    _heartbeat_interval_sec = std::min(static_cast<int>(interval), MIN_HEARTBEAT_LEN_SEC) / 2;
    spdlog::debug("[amqp] Using heartbeat interval = {}sec(s).", _heartbeat_interval_sec);
    return _heartbeat_interval_sec * 2;
}

bool amqp_asio_connection::reconnect()
{
    spdlog::info("[amqp] Connecting to AMQP broker.");
    if (_connection)
    {
        spdlog::info("[amqp] Closing existing AMQP connection.");
        _connection->close();
        delete _connection;
        _connection = nullptr;
    }
    if (_socket)
        close_socket(true);
    _socket = nullptr;

    boost::system::error_code ec;
    auto endpoints = _resolver.resolve(_host, std::to_string(_port), ec);
    if (ec)
    {
        spdlog::error(
            "[amqp] Failed resolving DN connecting to AMQP server {}! err: {}:{}",
            _host, ec.value(), ec.message());
        start_reconnect_timer();
        return false;
    }
    if (_socket && _connection)
        return false;
    _socket = std::make_shared<boost::asio::ip::tcp::socket>(_context.get_executor());
    boost::asio::connect(*_socket, endpoints, ec);
    if (ec)
    {
        spdlog::error(
            "[amqp] Failed connecting to AMQP server {}! err: {}:{}",
            _host, ec.value(), ec.message());
        start_reconnect_timer();
        return false;
    }

    spdlog::info("[amqp] Connected to AMQP broker. Starting handshake process.");
    _initializing = true;
    _connection = new AMQP::Connection(this, _login, _vhost);

    start_async_read();
    return true;
}

void amqp_asio_connection::disconnect()
{
    if (_connection)
        _connection->close();
    // close_socket(true);
}

bool amqp_asio_connection::reconnect(std::function<void()> onReady)
{
    _onReady = std::move(onReady);
    return reconnect();
}

void amqp_asio_connection::close_socket(bool active)
{
    spdlog::info("[amqp] Disconnecting AMQP broker connection.");
    _buffer_last_remaining = 0;
    if (!_socket)
        return;
    _socket->shutdown(boost::asio::socket_base::shutdown_both);

    boost::system::error_code ec = boost::system::error_code(boost::system::errc::operation_canceled, boost::system::system_category());
    _timer.cancel(ec);
    _socket->close(ec);
    _socket = nullptr;

    if (!active)
    {
        spdlog::info("[amqp] Scheduling reconnection in {} seconds.", _reconnect_interval_sec);
        start_reconnect_timer();
    }
}

void amqp_asio_connection::start_async_read()
{
    if (!_socket)
        return;
    SPDLOG_TRACE("[amqp] Starting async read. Last remaining bytes={}", _buffer_last_remaining);
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

void amqp_asio_connection::start_reconnect_timer()
{
    boost::system::error_code ec;
    _reconnect_timer.cancel(ec);
    _reconnect_timer.expires_from_now(boost::posix_time::seconds(_reconnect_interval_sec));
    _reconnect_timer.async_wait(boost::bind(&amqp_asio_connection::on_reconnection_timer_tick, this, boost::asio::placeholders::error));
}

void amqp_asio_connection::on_timer_tick(const boost::system::error_code& ec)
{
    if (ec)
        return;
    if (_connection && _connection->usable())
    {
        SPDLOG_DEBUG("[amqp] Sending heartbeat packet.");
        _connection->heartbeat();
    }

    start_heartbeat_timer();
}

void amqp_asio_connection::on_reconnection_timer_tick(const boost::system::error_code& ec)
{
    if (ec)
        return;
    reconnect(); // New reconnection timer will be set if fails.
}

void amqp_asio_connection::on_received(const boost::system::error_code& ec, size_t transferred)
{
    if (ec)
    {
        spdlog::error("[amqp] Failed receiving data from AMQP broker! Disconnecting. err:{}:{}", ec.value(), ec.message().c_str());
        if (_connection)
            _connection->fail(ec.message().c_str());
        close_socket();
        return;
    }
    if (!_connection)
        return;
    spdlog::trace("[amqp] Received {} bytes, in addition with {} bytes left from last read.", transferred, _buffer_last_remaining);
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

// This will be called on AMQP thread.
void amqp_context::on_ready()
{
    _available = false;
    _channel = new AMQP::Channel(_connection);
    _channel->declareExchange(_exchange, AMQP::ExchangeType::topic)
    .onError(
        [this](const char* msg) -> void {
            spdlog::error("[amqp] Error declaring AMQP exchange with name {}! msg:{}", _exchange, msg);
        }
    )
    .onSuccess(
        [this]() -> void {
            _available = true;
            spdlog::info("[amqp] Successfully set up exchange {}!", _exchange);
        }
    );
    _channel->declareExchange(_diag_exchange, AMQP::ExchangeType::fanout);
}

amqp_context::amqp_context(const config::config_t options)
    : _connection(
        (*options)["amqp-host"].as<std::string>(),
        (*options)["amqp-port"].as<int>(),
        AMQP::Login(
            (*options)["amqp-user"].as<std::string>(),
            (*options)["amqp-password"].as<std::string>()),
        (*options)["amqp-vhost"].as<std::string>(),
        (*options)["amqp-reconnect-interval-sec"].as<int>()),
      _exchange((*options)["amqp-exchange"].as<std::string>()),
      _diag_exchange((*options)["amqp-diag-exchange"].as<std::string>()),
      _write_buf(new unsigned char[write_buf_default_size]),
      _write_buf_len(write_buf_default_size)
{
    _connection.post([this]() {
        _connection.reconnect(std::bind(&amqp_context::on_ready, this));
    });
}

amqp_context::~amqp_context()
{
    _connection.disconnect();
}

void amqp_context::post_payload(const std::string_view routing_key, unsigned char const* payload, size_t len)
{
    auto buf = new unsigned char[len];
    auto routing_key_buf = std::string(routing_key);
    std::memcpy(buf, payload, len);
    _connection.post([this, routing_key_buf, buf, len]() {
        if (!_available || !_connection.connection() || !_channel)
        {
            delete[] buf;
            return;
        }
        _channel->publish(_exchange, routing_key_buf, reinterpret_cast<char*>(buf), len)
        .onError([this, &routing_key_buf](const char* msg) -> void
        {
            spdlog::warn("[amqp] Error sending payload {} with routing key {} to exchange! msg:{}", _exchange, routing_key_buf, msg);
        });
        delete[] buf;
    });
}

void amqp_context::post_diag_payload(unsigned char const* payload, size_t len)
{
    auto buf = new unsigned char[len];
    std::memcpy(buf, payload, len);
    _connection.post([this, buf, len]() {
        if (!_available || !_connection.connection() || !_channel)
        {
            delete[] buf;
            return;
        }
        _channel->publish(_diag_exchange, "", reinterpret_cast<char*>(buf), len)
            .onError([this](const char* msg) -> void {
                spdlog::warn("[amqp] Error sending diagnostic payload {} to exchange! msg:{}", _exchange, msg);
            });
        delete[] buf;
    });
}
}
