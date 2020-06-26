#include "bili_conn_ws.h"

#include "bili_packet.h"
#include "bilibili_connection_manager.h"

#include <boost/bind.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>

namespace vNerve::bilibili
{
boost::asio::ssl::context* ssl_context = nullptr;

bool configure_ssl_context()
{
    ssl_context = new boost::asio::ssl::context{boost::asio::ssl::context::tls_client};
    try
    {
        ssl_context->load_verify_file("cacert.pem");
    }
    catch (boost::system::system_error& err)
    {
        spdlog::critical("[conn] Failed to initialize ssl_context from file cacert.pem! Ensure you have a valid CA cert file. err:{}:{}", err.code().value(), err.code().message());
        throw;
    }
    spdlog::info("[conn] Initialized SSL Context from cacert.pem.");
    return true;
}
static const bool ssl_context_configured = configure_ssl_context();

bilibili_connection_websocket::bilibili_connection_websocket(
    bilibili_connection_manager* session, int room_id)
    : _read_buffer(session->get_options()["read-buffer"].as<size_t>()),
      _session(session),
      _resolver(session->get_io_context()),
      _ws_stream(make_strand(session->get_io_context()), *ssl_context),
      _heartbeat_timer(std::make_unique<boost::asio::deadline_timer>(_session->get_io_context())),
      _room_id(room_id),
      _heartbeat_interval_sec(_session->get_options()["heartbeat-timeout"].as<int>())
{
}

bilibili_connection_websocket::~bilibili_connection_websocket()
{
    close(false);
}

void bilibili_connection_websocket::init()
{
    async_fetch_bilibili_live_config(_session->get_io_context(), _resolver, _session->get_options_ptr(), _room_id,
                                     std::bind(&bilibili_connection_websocket::on_config_fetched, shared_from_this(), std::placeholders::_1),
                                     [&]() -> void {
                                         _session->on_room_failed(_room_id);
                                     });
}

void bilibili_connection_websocket::on_config_fetched(bilibili_live_config const& config)
{
    _token = config.token;
    spdlog::debug(
        "[session] Connecting room {} with server {}:{}, resolving DN.",
        _room_id, config.host, config.port);
    _resolver.async_resolve(
            config.host,
            std::to_string(config.port),
            boost::bind(&bilibili_connection_websocket::on_resolved, shared_from_this(), boost::asio::placeholders::error, boost::asio::placeholders::iterator));
}

void bilibili_connection_websocket::on_resolved(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::results_type endpoints)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug(
                "[conn] Cancelling connecting(resolving) to room {}.",
                _room_id);
            return;
        }
        spdlog::warn(
            "[conn] Failed resolving DN connecting to room {}! err: {}:{}",
            _room_id, err.value(), err.message());
        _session->on_room_failed(_room_id);
        return;
    }
    auto& lowest = get_lowest_layer(_ws_stream);
    lowest.expires_after(std::chrono::seconds(10));
    async_connect(lowest.socket(), endpoints, boost::bind(&bilibili_connection_websocket::on_connected, shared_from_this(), boost::asio::placeholders::error));
}

void bilibili_connection_websocket::on_connected(const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[conn] Cancelling connecting to room {}.",
                          _room_id);
            return;
        }
        spdlog::warn("[conn] Failed connecting to room {}! Unable to establish tcp connection. err: {}:{}",
                     _room_id, err.value(), err.message());
        close(true);
        return;
    }

    spdlog::debug("[conn] Connected to room {}. Setting up SSL & websocket protocol.", _room_id);

    get_lowest_layer(_ws_stream).expires_after(std::chrono::seconds(10));
    _ws_stream.next_layer().async_handshake(
        boost::asio::ssl::stream_base::client,
        boost::bind(&bilibili_connection_websocket::on_ssl_handshake, shared_from_this(), boost::asio::placeholders::error));
}

void bilibili_connection_websocket::on_ssl_handshake(const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[conn] Cancelling connecting to room {}.",
                          _room_id);
            return;
        }
        spdlog::warn("[conn] Failed connecting to room {}! Unable to perform SSL handshake. err: {}:{}",
                     _room_id, err.value(), err.message());
        close(true);
        return;
    }

    spdlog::debug("[conn] [room={}] SSL handshake succeeded. Setting up websocket protocol.", _room_id);

    get_lowest_layer(_ws_stream).expires_never();
    _ws_stream.set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
    _ws_stream.set_option(boost::beast::websocket::stream_base::decorator(
        [this](boost::beast::websocket::request_type& req) {
            req.set(boost::beast::http::field::user_agent, _session->get_options()["chat-config-user-agent"].as<std::string>());
            req.set(boost::beast::http::field::accept_language, "zh-CN,zh;q=0.9");
            req.set(boost::beast::http::field::accept_encoding, "gzip, deflate");
            req.set(boost::beast::http::field::pragma, "no-cache");
            req.set(boost::beast::http::field::cache_control, "no-cache");
        }));
    auto config = _session->get_options();
    auto host = config["chat-server"].as<std::string>() + ':' + std::to_string(config["chat-server-port"].as<int>());
    _ws_stream.async_handshake(host, config["chat-server-endpoint"].as<std::string>(), boost::beast::bind_front_handler(&bilibili_connection_websocket::on_handshake, shared_from_this()));
}

void bilibili_connection_websocket::on_handshake(const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[conn] Cancelling connecting to room {}.",
                          _room_id);
            return;
        }
        spdlog::warn("[conn] Failed connecting to room {} when WebSocket handshaking! err: {}:{}",
                     _room_id, err.value(), err.message());
        close(true);
        return;
    }

    spdlog::debug("[conn] [room={}] WebSocket handshake completed. Setting up protocol.", _room_id);

    start_read();

    auto str = new std::string(generate_join_room_packet(
        _room_id, _session->get_options()["protocol-ver"].as<int>(), _token));
    auto buffer = boost::asio::buffer(*str);
    SPDLOG_TRACE(
        "[conn] [room={}] Sending handshake packet with payload(len={}): {:Xs}",
        _room_id, str->length(),
        spdlog::to_hex(str->c_str(), str->c_str() + str->length()));
    _ws_stream.async_write(
        buffer, boost::bind(&bilibili_connection_websocket::on_join_room_sent, shared_from_this(),
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred, str));
    // Don't need a sending queue
    // Because the sending frequency is low.
}

void bilibili_connection_websocket::on_join_room_sent(
    const boost::system::error_code& err, const size_t transferred,
    std::string* buf)
{
    delete buf;  // delete sending buffer.
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted || err == boost::beast::websocket::error::closed)
        {
            SPDLOG_DEBUG("[conn] Cancelling handshake sending.");
            return;  // closing socket.
        }
        spdlog::warn(
            "[conn] [room={}] Failed sending handshake packet! err:{}: {}",
            _room_id, err.value(), err.message());
        close(true);
    }

    SPDLOG_DEBUG(
        "[conn] [room={}] Sent handshake packet. Bytes transferred: {}",
        _room_id, transferred);
    reschedule_timer();
}

void bilibili_connection_websocket::reschedule_timer()
{
    SPDLOG_DEBUG("[conn] [room={}] Scheduling heartbeat, interval={}",
                 _room_id, _heartbeat_interval_sec);
    _heartbeat_timer->expires_from_now(
        boost::posix_time::seconds(_heartbeat_interval_sec));
    _heartbeat_timer->async_wait(
        boost::bind(&bilibili_connection_websocket::on_heartbeat_tick, shared_from_this(),
                    boost::asio::placeholders::error));
}

void bilibili_connection_websocket::on_heartbeat_tick(
    const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted || err == boost::beast::websocket::error::closed)
        {
            SPDLOG_DEBUG("[conn] Cancelling heartbeat timer.");
            return;  // closing socket.
        }
        spdlog::warn("[conn] [room={}] Error in heartbeat tick! err:{}: {}",
                     _room_id, err.value(), err.message());
        return;  // closing socket.
    }

    auto& buf = _session->get_heartbeat_buffer();
    auto buf_ptr = reinterpret_cast<const char*>(buf.data());
    SPDLOG_DEBUG(
        "[conn] [room={}] Sending heartbeat packet with payload(len={}): {:Xs}",
        _room_id, buf.size(), spdlog::to_hex(buf_ptr, buf_ptr + buf.size()));
    _ws_stream.async_write(
        buf, boost::bind(&bilibili_connection_websocket::on_heartbeat_sent, shared_from_this(),
                         boost::asio::placeholders::error,
                         boost::asio::placeholders::bytes_transferred));

    reschedule_timer();
}

void bilibili_connection_websocket::on_heartbeat_sent(
    const boost::system::error_code& err, const size_t transferred)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted || err == boost::beast::websocket::error::closed)
        {
            SPDLOG_DEBUG("[conn] Cancelling heartbeat sending.");
            return;  // closing socket.
        }
        spdlog::warn(
            "[conn] [room={}] Failed sending heartbeat packet! err:{}: {}",
            _room_id, err.value(), err.message());
        close(true);
    }
    // nothing to do.
    SPDLOG_DEBUG(
        "[conn] [room={}] Sent heartbeat packet. Bytes transferred: {}",
        _room_id, transferred);
}

void bilibili_connection_websocket::start_read()
{
    SPDLOG_TRACE("[conn] [room={}] Starting next async read.", _room_id);
    _read_buffer.clear();
    _ws_stream.async_read(
        _read_buffer,
        boost::bind(&bilibili_connection_websocket::on_receive, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void bilibili_connection_websocket::on_receive(
    const boost::system::error_code& err, const size_t transferred)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted || err == boost::beast::websocket::error::closed)
        {
            SPDLOG_DEBUG("[conn] Cancelling async reading.");
            return;  // closing socket.
        }
        spdlog::warn("[conn] [room={}] Error in async recv! err:{}: {}",
                     _room_id, err.value(), err.message());
        close(true);
    }

    SPDLOG_DEBUG("[conn] [room={}] Received data block(len={})", _room_id,
                 transferred);
    try
    {
        handle_buffer(reinterpret_cast<unsigned char*>(_read_buffer.data().data()), transferred, _read_buffer.size(), 0,  // No need to concat packets ourselves
                      _room_id, std::bind(&bilibili_connection_manager::on_room_data, _session, _room_id, std::placeholders::_1));
    }
    catch (malformed_packet&)
    {
        close(true);
        return;
    }

    start_read();
}

void bilibili_connection_websocket::close(const bool failed)
{
    if (_closed)
        return;
    _closed = true;

    boost::system::error_code ec;
    _heartbeat_timer->cancel(ec);

    _ws_stream.async_close(boost::beast::websocket::close_code::normal,
                           boost::beast::bind_front_handler(
                               &bilibili_connection_websocket::on_closed,
                               shared_from_this()));

    if (failed)
        _session->on_room_failed(_room_id);
    _session->on_room_closed(_room_id);
}

void bilibili_connection_websocket::on_closed(const boost::system::error_code& err)
{
    if (err)
    {
        spdlog::warn("[conn] [room={}] Error when closing: {}:{}", _room_id, err.value(), err.message());
    }
    // whatever, closed
}
}
