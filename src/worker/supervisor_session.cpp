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
    /*if (zmsg_size(msg) != 1)
        return;
    auto frame = zmsg_pop(msg);
    auto payload = zframe_data(frame);
    auto payload_len = zframe_size(frame);

    if (payload_len < 5)
    {
        return;
        // todo log?
    }

    auto op_code = payload[0];
    switch (op_code)
    {
    case assign_room_code:
    {
    }
    break;
    case unassign_room_code:
    {
    }
    break;
    default:
        break;
    }*/
}

void supervisor_session::on_data(unsigned char* data, size_t len)
{
    /*auto frame = zframe_frommem(data, len, deleter, data);
    auto msg = zmsg_new();
    zmsg_prepend(msg, &frame);
    _connection.publish_msg(msg);
    */
}
}
