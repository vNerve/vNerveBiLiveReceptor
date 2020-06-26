#include "bili_conn_plain_tcp.h"

#include "bili_packet.h"
#include "bilibili_connection_manager.h"

#include <boost/bind.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>

vNerve::bilibili::bilibili_connection_plain_tcp::bilibili_connection_plain_tcp(
    bilibili_connection_manager* session, int room_id, std::string_view token)
    : _read_buffer_size(session->get_options()["read-buffer"].as<size_t>()),
      _session(session),
      _socket(std::make_shared<boost::asio::ip::tcp::socket>(session->_context.get_executor())),
      _heartbeat_timer(std::make_unique<boost::asio::deadline_timer>(
          _session->get_io_context())),
      _room_id(room_id),
      _token(token),
      _heartbeat_interval_sec(
          _session->get_options()["heartbeat-timeout"].as<int>())
{
    spdlog::info("[conn] [room={}] Established connection to server.", room_id);
    _read_buffer_ptr =
        std::unique_ptr<unsigned char[]>(new unsigned char[_read_buffer_size + 1]); // extra space for \0
}

vNerve::bilibili::bilibili_connection_plain_tcp::~bilibili_connection_plain_tcp()
{
    close(false);
}

void vNerve::bilibili::bilibili_connection_plain_tcp::reschedule_timer()
{
    SPDLOG_DEBUG("[conn] [room={}] Scheduling heartbeat, interval={}",
                  _room_id, _heartbeat_interval_sec);
    _heartbeat_timer->expires_from_now(
        boost::posix_time::seconds(_heartbeat_interval_sec));
    _heartbeat_timer->async_wait(
        boost::bind(&bilibili_connection_plain_tcp::on_heartbeat_tick, shared_from_this(),
                    boost::asio::placeholders::error));
}

void vNerve::bilibili::bilibili_connection_plain_tcp::start_read()
{
    SPDLOG_TRACE(
        "[conn] [room={}] Starting next async read. offset={}, size={}/{}",
        _room_id, _read_buffer_offset, _read_buffer_size - _read_buffer_offset,
        _read_buffer_size);
    _socket->async_receive(
        boost::asio::buffer(_read_buffer_ptr.get() + _read_buffer_offset,
                            _read_buffer_size - _read_buffer_offset),
        boost::bind(&bilibili_connection_plain_tcp::on_receive, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void vNerve::bilibili::bilibili_connection_plain_tcp::on_connected(const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[session] Cancelling connecting to room {}.",
                          _room_id);
            return;
        }
        spdlog::warn("[session] Failed connecting to room {}! err: {}:{}",
                     _room_id, err.value(), err.message());
        close(true);
        return;
    }

    spdlog::debug("[conn] Connected to room {}. Setting up connection protocol.", _room_id);
    start_read();

    auto str = new std::string(generate_join_room_packet(
        _room_id, _session->get_options()["protocol-ver"].as<int>(), _token));
    auto buffer = boost::asio::buffer(*str);
    SPDLOG_TRACE(
        "[conn] [room={}] Sending handshake packet with payload(len={}): {:Xs}",
        _room_id, str->length(),
        spdlog::to_hex(str->c_str(), str->c_str() + str->length()));
    _socket->async_send(
        buffer, boost::bind(&bilibili_connection_plain_tcp::on_join_room_sent, shared_from_this(),
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred, str));
    // Don't need a sending queue
    // Because the sending frequency is low.
}

void vNerve::bilibili::bilibili_connection_plain_tcp::init(const boost::asio::ip::tcp::resolver::iterator& endpoints)
{
    async_connect(*_socket, endpoints,
        boost::bind(&bilibili_connection_plain_tcp::on_connected, shared_from_this(), boost::asio::placeholders::error));
}

void vNerve::bilibili::bilibili_connection_plain_tcp::close(const bool failed)
{
    if (_closed)
        return;
    _closed = true;

    boost::system::error_code ec;
    if (!_socket)
        return;
    _socket->shutdown(boost::asio::socket_base::shutdown_both, ec);
    _socket->cancel(ec);
    _socket->close(ec);
    _heartbeat_timer->cancel(ec);

    _socket.reset();

    if (failed)
        _session->on_room_failed(_room_id);
    _session->on_room_closed(_room_id);
}

void vNerve::bilibili::bilibili_connection_plain_tcp::on_join_room_sent(
    const boost::system::error_code& err, const size_t transferred,
    std::string* buf)
{
    delete buf;  // delete sending buffer.
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
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

void vNerve::bilibili::bilibili_connection_plain_tcp::on_heartbeat_sent(
    const boost::system::error_code& err, const size_t transferred)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
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

void vNerve::bilibili::bilibili_connection_plain_tcp::on_heartbeat_tick(
    const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
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
    _socket->async_send(
        buf, boost::bind(&bilibili_connection_plain_tcp::on_heartbeat_sent, shared_from_this(),
                         boost::asio::placeholders::error,
                         boost::asio::placeholders::bytes_transferred));

    reschedule_timer();
}

void vNerve::bilibili::bilibili_connection_plain_tcp::on_receive(
    const boost::system::error_code& err, const size_t transferred)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
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
        auto [new_offset, new_skipping_bytes] =
            handle_buffer(_read_buffer_ptr.get(), transferred, _read_buffer_size, _skipping_bytes,
                          _room_id, std::bind(&bilibili_connection_manager::on_room_data, _session, _room_id, std::placeholders::_1));
        _read_buffer_offset = new_offset;
        _skipping_bytes = new_skipping_bytes;
    }
    catch (malformed_packet&)
    {
        close(true);
        return;
    }

    start_read();
}
