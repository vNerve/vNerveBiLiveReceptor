#include "simple_worker_proto_generator.h"

#include "simple_worker_proto.h"
#include <boost/asio/detail/socket_ops.hpp>

namespace vNerve::bilibili::worker_supervisor
{

std::pair<unsigned char*, size_t> generate_room_basic_packet(int room_place)
{
    const int packet_length = simple_message_header_length + worker_ready_payload_length;
    auto packet = new unsigned char[packet_length];
    *reinterpret_cast<int*>(packet) = boost::asio::detail::socket_ops::host_to_network_long(worker_ready_payload_length);

    *reinterpret_cast<int*>(packet + 5) = boost::asio::detail::socket_ops::host_to_network_long(room_place);

    return std::pair(packet, packet_length);
}

std::pair<unsigned char*, size_t> generate_room_failed_packet(room_id_t room_id)
{
    auto pair = generate_room_basic_packet(room_id);
    pair.first[simple_message_header_length] = room_failed_code;
    return pair;
}

std::pair<unsigned char*, size_t> generate_worker_ready_packet(int max_rooms)
{
    auto pair = generate_room_basic_packet(max_rooms);
    pair.first[simple_message_header_length] = worker_ready_code;
    return pair;
}
}
