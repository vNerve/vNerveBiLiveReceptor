#include "bili_conn.h"

#include "bili_session.h"
#include "bili_packet.h"

#include <boost/bind.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/fmt/bin_to_hex.h>

vNerve::bilibili::bilibili_connection::bilibili_connection(const std::shared_ptr<boost::asio::ip::tcp::socket> socket, const std::shared_ptr<bilibili_session> session, int room_id)
: _read_buffer_size(session->get_options()["read-buffer"].as<size_t>()),
_session(session),
_socket(socket),
_heartbeat_timer(std::make_unique<boost::asio::deadline_timer>(_session->get_io_context())),
_room_id(room_id),
_heartbeat_interval_sec(_session->get_options()["heartbeat-timeout"].as<int>())
{
    spdlog::info("[conn] [room={}] Established connection to server.", room_id);
    _read_buffer_ptr = std::unique_ptr<unsigned char[]>(new unsigned char[_read_buffer_size]);
    start_read();

    auto str = new std::string(generate_join_room_packet(room_id, session->get_options()["protocol-ver"].as<int>()));
    auto buffer = boost::asio::buffer(*str);
    spdlog::debug("[conn] [room={}] Sending handshake packet with payload(len={}): {:Xs}", room_id, str->length(), spdlog::to_hex(str->c_str(), str->c_str() + str->length()));
    socket->async_send(buffer,
        boost::bind(&bilibili_connection::on_join_room_sent, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, str));
}

vNerve::bilibili::bilibili_connection::~bilibili_connection()
{
    try
    {
        _socket->shutdown(boost::asio::socket_base::shutdown_both);
        _socket->cancel();
        _socket->close();
        _heartbeat_timer->cancel();
    } catch (boost::system::system_error& ex)
    {
        spdlog::warn("[conn] [room={}] Failed shutting down connection! err:{}:{}:{}", ex.code().value(), ex.code().message(), ex.what());
    }
}

void vNerve::bilibili::bilibili_connection::reschedule_timer()
{
    spdlog::debug("[conn] [room={}] Scheduling heartbeat, interval={}", _room_id, _heartbeat_interval_sec);
    _heartbeat_timer->expires_from_now(boost::posix_time::seconds(_heartbeat_interval_sec));
    _heartbeat_timer->async_wait(boost::bind(&bilibili_connection::on_heartbeat_tick, this, boost::asio::placeholders::error));
}

void vNerve::bilibili::bilibili_connection::start_read()
{
    spdlog::trace("[conn] [room={}] Starting next async read. offset={}, size={}/{}", _room_id
        , _read_buffer_offset, _read_buffer_size - _read_buffer_offset, _read_buffer_size);
    _socket->async_receive(
        boost::asio::buffer(_read_buffer_ptr.get() + _read_buffer_offset, _read_buffer_size - _read_buffer_offset),
        boost::bind(&bilibili_connection::on_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void vNerve::bilibili::bilibili_connection::on_join_room_sent(const boost::system::error_code& err, const size_t transferred, std::string* buf)
{
    delete buf; // delete sending buffer.
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[conn] [room={}] Cancelling handshake sending.", _room_id);
            return; // closing socket.
        }
        // TODO error handling
        spdlog::warn("[conn] [room={}] Failed sending handshake packet! err:{}: {}", _room_id, err.value(), err.message());
    }

    spdlog::debug("[conn] [room={}] Sent handshake packet. Bytes transferred: {}", _room_id, transferred);
    reschedule_timer();
}

void vNerve::bilibili::bilibili_connection::on_heartbeat_sent(const boost::system::error_code& err, const size_t transferred) const
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[conn] [room={}] Cancelling heartbeat sending.", _room_id);
            return; // closing socket.
        }
        // TODO error handling
        spdlog::warn("[conn] [room={}] Failed sending heartbeat packet! err:{}: {}", _room_id, err.value(), err.message());
    }
    // nothing to do.
    spdlog::debug("[conn] [room={}] Sent heartbeat packet. Bytes transferred: {}", _room_id, transferred);
}

void vNerve::bilibili::bilibili_connection::on_heartbeat_tick(const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[conn] [room={}] Cancelling heartbeat timer.", _room_id);
            return; // closing socket.
        }
        spdlog::warn("[conn] [room={}] Error in heartbeat tick! err:{}: {}", _room_id, err.value(), err.message());
    }

    auto& buf = _session->get_heartbeat_buffer();
    auto buf_ptr = reinterpret_cast<const char*>(buf.data());
    spdlog::debug("[conn] [room={}] Sending heartbeat packet with payload(len={}): {:Xs}", _room_id, buf.size(), spdlog::to_hex(buf_ptr, buf_ptr + buf.size()));
    _socket->async_send(buf,
        boost::bind(&bilibili_connection::on_heartbeat_sent, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

    reschedule_timer();
}

void vNerve::bilibili::bilibili_connection::on_receive(const boost::system::error_code& err, const size_t transferred)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[conn] [room={}] Cancelling async reading.", _room_id);
            return; // closing socket.
        }
        // TODO error handling
    }

    spdlog::debug("[conn] [room={}] Received data block(len={})", _room_id, transferred);
    auto [new_offset, new_skipping_bytes] = handle_buffer(_read_buffer_ptr.get(), transferred, _read_buffer_size, _skipping_bytes);
    _read_buffer_offset = new_offset;
    _skipping_bytes = new_skipping_bytes;
    start_read();
}
