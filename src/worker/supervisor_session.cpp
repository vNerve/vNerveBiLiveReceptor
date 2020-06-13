#include "supervisor_session.h"

#include "simple_worker_proto.h"
#include "simple_worker_proto_generator.h"

#include <boost/asio/detail/socket_ops.hpp>
#include <utility>
#include <spdlog/spdlog.h>

namespace vNerve::bilibili::worker_supervisor
{
void deleter_unsigned_char_array(unsigned char* buf)
{
    delete[] buf;
}

supervisor_session::supervisor_session(config::config_t config, room_operation_handler on_open_connection, room_operation_handler on_close_connection)
    : _config(config),
      _connection(config,
                  std::bind(&supervisor_session::on_supervisor_message, this, std::placeholders::_1, std::placeholders::_2),
                  std::bind(&supervisor_session::on_supervisor_connected, this)),
      _max_rooms((*_config)["max-rooms"].as<int>()),
      _on_open_connection(std::move(on_open_connection)),
      _on_close_connection(std::move(on_close_connection))
{

}

supervisor_session::~supervisor_session()
{
}

void supervisor_session::on_supervisor_connected()
{
    auto [packet, packet_length] = generate_worker_ready_packet(_max_rooms);

    spdlog::info("[sv_sess] Connected to supervisor. Sending ready packet with max_rooms={}", _max_rooms);
    _connection.publish_msg(packet, packet_length, deleter_unsigned_char_array);
}

void supervisor_session::on_supervisor_message(unsigned char* msg, size_t len)
{
    if (len < assign_unassign_payload_length)  // OP_CODE + ROOM_ID
    {
        SPDLOG_TRACE("[sv_sess] Malformed packet: len={} < {}", len, assign_unassign_payload_length);
        return;
    }

    unsigned char op_code = *msg;
    int room_id = boost::asio::detail::socket_ops::host_to_network_long(*reinterpret_cast<int*>(msg + 1));

    switch (op_code)
    {
    case assign_room_code:
    {
        SPDLOG_DEBUG("[sv_sess] Reveived Assign room packet. room_id={}", room_id);
        _on_open_connection(room_id);
    }
        break;
    case unassign_room_code:
    {
        SPDLOG_DEBUG("[sv_sess] Reveived Unassign room packet. room_id={}", room_id);
        _on_close_connection(room_id);
    }
        break;
    default:
        SPDLOG_DEBUG("[sv_sess] Invalid sv packet. opcode=", op_code);
        break;
    }
}

void supervisor_session::on_message(room_id_t room_id, borrowed_message const* msg)
{
    auto [packet, packet_length] = generate_worker_data_packet(room_id, msg);

    SPDLOG_TRACE("[sv_sess] Sending Worker data packet. room_id={}, len={}", room_id, packet_length);
    _connection.publish_msg(packet, packet_length, deleter_unsigned_char_array);
}

void supervisor_session::on_room_failed(room_id_t room_id)
{
    auto [packet, packet_length] = generate_room_failed_packet(room_id);

    SPDLOG_DEBUG("[sv_sess] Sending Room failed packet. room_id=", room_id);
    _connection.publish_msg(packet, packet_length, deleter_unsigned_char_array);
}

void supervisor_session::join()
{
    _connection.join();
}

void supervisor_session::on_data(unsigned char* data, size_t len, supervisor_buffer_deleter deleter)
{
    if (len < simple_message_header_length)
    {
        SPDLOG_DEBUG("[sv_sess] Failed to send packet. Malformed data: len={}<simple_message_header_length", len);
        deleter(data);
    }
    _connection.publish_msg(data, len, deleter_unsigned_char_array);
}
}
