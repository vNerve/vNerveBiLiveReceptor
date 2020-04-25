#include "supervisor_session.h"

#include "simple_worker_proto.h"

#include <boost/asio/detail/socket_ops.hpp>

namespace vNerve::bilibili::worker_supervisor
{
void deleter_unsigned_char_array(unsigned char* buf)
{
    delete[] buf;
}

supervisor_session::supervisor_session(config::config_t config)
    : _config(config),
      _connection(config,
                  std::bind(&supervisor_session::on_supervisor_message,
                            shared_from_this(), std::placeholders::_1, std::placeholders::_2),
                  std::bind(&supervisor_session::on_supervisor_connected, shared_from_this()))
{

}

void supervisor_session::on_supervisor_connected()
{
    const int packet_length = simple_message_header_length + worker_ready_payload_length;
    auto packet = new unsigned char[packet_length];
    *reinterpret_cast<int*>(packet) = boost::asio::detail::socket_ops::host_to_network_long(worker_ready_payload_length);
    packet[4] = worker_ready_code;
    *reinterpret_cast<int*>(packet + 5) = boost::asio::detail::socket_ops::host_to_network_long((*_config)["max-rooms"].as<int>());

    // TODO log
    _connection.publish_msg(packet, packet_length, deleter_unsigned_char_array);
}

void supervisor_session::on_supervisor_message(unsigned char* msg, size_t len)
{
    if (len < assign_unassign_payload_length)  // OP_CODE + ROOM_ID
    {
        // todo log
        return;
    }

    unsigned char op_code = *msg;
    int room_id = boost::asio::detail::socket_ops::host_to_network_long(*reinterpret_cast<int*>(msg + 1));

    switch (op_code)
    {
    case assign_room_code:
    {
        // TODO impl
    }
        break;
    case unassign_room_code:
    {
        // TODO impl
    }
        break;
    default:
        break;
        // todo log
    }
}

void supervisor_session::on_data(unsigned char* data, size_t len, supervisor_buffer_deleter deleter)
{
    if (len < simple_message_header_length)
    {
        // TODO log
        deleter(data);
    }
    _connection.publish_msg(data, len, deleter_unsigned_char_array);
}
}
