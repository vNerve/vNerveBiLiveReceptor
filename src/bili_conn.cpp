#include "bili_conn.h"

#include "bili_protocol.h"
#include "bili_session.h"
#include "bili_packet.h"

#include <boost/bind.hpp>

vNerve::bilibili::bilibili_connection::bilibili_connection(const std::shared_ptr<boost::asio::ip::tcp::socket> socket, const std::shared_ptr<bilibili_session> session, int room_id)
    : _session(session), _socket(socket),
_heartbeat_timer(std::make_unique<boost::asio::deadline_timer>(_session->get_io_context())),
_room_id(room_id),
_heartbeat_interval_sec(_session->get_options()["heartbeat-timeout"].as<int>()),
_read_buffer_size(session->get_options()["read-buffer"].as<size_t>())
{
    _read_buffer_ptr = std::unique_ptr<unsigned char[]>(new unsigned char[_read_buffer_size]);
    start_read();

    auto str = new std::string(generate_join_room_packet(room_id, session->get_options()["protocol-ver"].as<int>()));
    auto buffer = boost::asio::buffer(*str);
    socket->async_send(buffer,
        boost::bind(&bilibili_connection::on_join_room_sent, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred, str));
}

vNerve::bilibili::bilibili_connection::~bilibili_connection()
{
    // TODO error handling
    _heartbeat_timer->cancel();
}

void vNerve::bilibili::bilibili_connection::reschedule_timer()
{
    _heartbeat_timer->expires_from_now(boost::posix_time::seconds(_heartbeat_interval_sec));
    _heartbeat_timer->async_wait(boost::bind(&bilibili_connection::on_heartbeat_tick, this, boost::asio::placeholders::error));
}

void vNerve::bilibili::bilibili_connection::start_read()
{
    _socket->async_receive(
        boost::asio::buffer(_read_buffer_ptr.get() + _read_buffer_offset, _read_buffer_size - _read_buffer_offset),
        boost::bind(&bilibili_connection::on_receive, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

void vNerve::bilibili::bilibili_connection::on_join_room_sent(const boost::system::error_code& err, size_t transferred, std::string* buf)
{
    delete buf; // delete sending buffer.
    if (err)
    {
        // TODO error handling
    }

    reschedule_timer();
}

void vNerve::bilibili::bilibili_connection::on_heartbeat_sent(const boost::system::error_code& err, size_t)
{
    if (err)
    {
        // TODO error handling
    }
    // nothing to do.
}

void vNerve::bilibili::bilibili_connection::on_heartbeat_tick(const boost::system::error_code& err)
{
    if (err == boost::asio::error::operation_aborted) return; // closing socket.
    _socket->async_send(_session->get_heartbeat_buffer(),
        boost::bind(&bilibili_connection::on_heartbeat_sent, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));

    reschedule_timer();
}

void vNerve::bilibili::bilibili_connection::on_receive(const boost::system::error_code& err, size_t transferred)
{
    if (err)
    {
        // TODO error handling
    }

    auto [new_offset, new_skipping_bytes] = handle_buffer(_read_buffer_ptr.get(), transferred, _read_buffer_size, _skipping_bytes);
    _read_buffer_offset = new_offset;
    _skipping_bytes = new_skipping_bytes;
    start_read();
}
