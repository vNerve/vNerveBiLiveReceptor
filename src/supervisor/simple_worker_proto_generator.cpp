#include "simple_worker_proto_generator.h"

#include <boost/asio/detail/socket_ops.hpp>

namespace vNerve::bilibili::worker_supervisor
{
std::pair<unsigned char*, size_t> generate_assign_unassign_base_packet(room_id_t room_id)
{
    auto size = simple_message_header_length + assign_unassign_payload_length;
    auto buf = new unsigned char[size];
    *reinterpret_cast<simple_message_header*>(buf) = boost::asio::detail::socket_ops::host_to_network_long(assign_unassign_payload_length);
    *reinterpret_cast<simple_message_header*>(buf + simple_message_header_length + 1) = boost::asio::detail::socket_ops::host_to_network_long(room_id);

    return std::pair(buf, size);
}

std::pair<unsigned char*, size_t> generate_assign_packet(room_id_t room_id)
{
    auto buf = generate_assign_unassign_base_packet(room_id);
    buf.first[simple_message_header_length] = assign_room_code;
    return buf;
}

std::pair<unsigned char*, size_t> generate_unassign_packet(room_id_t room_id)
{
    auto buf = generate_assign_unassign_base_packet(room_id);
    buf.first[simple_message_header_length] = unassign_room_code;
    return buf;
}

}
