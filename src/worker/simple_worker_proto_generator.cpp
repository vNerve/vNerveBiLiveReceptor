#include "simple_worker_proto_generator.h"

#include "simple_worker_proto.h"
#include "borrowed_message.h"
#include <boost/asio/detail/socket_ops.hpp>

namespace vNerve::bilibili::worker_supervisor
{

std::pair<unsigned char*, size_t> generate_room_basic_packet(int room_place, size_t payload_size)
{
    const int packet_length = simple_message_header_length + payload_size;
    auto packet = new unsigned char[packet_length];
    *reinterpret_cast<int*>(packet) = boost::asio::detail::socket_ops::host_to_network_long(payload_size);

    *reinterpret_cast<int*>(packet + simple_message_header_length + 1) = boost::asio::detail::socket_ops::host_to_network_long(room_place);

    return std::pair(packet, packet_length);
}

std::pair<unsigned char*, size_t> generate_room_failed_packet(room_id_t room_id)
{
    auto pair = generate_room_basic_packet(room_id, room_failed_payload_length);
    pair.first[simple_message_header_length] = room_failed_code;
    return pair;
}

std::pair<unsigned char*, size_t> generate_worker_ready_packet(int max_rooms, std::string_view auth_code)
{
    auto pair = generate_room_basic_packet(max_rooms, worker_ready_payload_length);
    pair.first[simple_message_header_length] = worker_ready_code;
    std::memset(reinterpret_cast<char*>(pair.first + simple_message_header_length + 5), 0, auth_code_size);
    std::memcpy(reinterpret_cast<char*>(pair.first + simple_message_header_length + 5), auth_code.data(), auth_code.size());
    return pair;
}

std::pair<unsigned char*, size_t> generate_worker_data_packet(room_id_t room_id, borrowed_message const* msg)
{
    const size_t payload_length = worker_data_payload_header_length + msg->size();
    const size_t packet_length = simple_message_header_length + payload_length;
    auto packet = new unsigned char[packet_length];

    auto ptr = packet;
    *reinterpret_cast<int*>(ptr) = boost::asio::detail::socket_ops::host_to_network_long(payload_length); // LEN
    ptr += simple_message_header_length;
    *(ptr) = worker_data_code;                                                                                                          // OP_CODE
    ptr++;
    *reinterpret_cast<int*>(ptr) = boost::asio::detail::socket_ops::host_to_network_long(room_id);                      // ROOM
    ptr += room_id_length;
    *reinterpret_cast<int*>(ptr) = boost::asio::detail::socket_ops::host_to_network_long(msg->crc32);                       // CHECKSUM
    ptr += crc_32_length;
    std::memcpy(ptr, msg->routing_key, routing_key_max_size);
    ptr += routing_key_max_size;
    msg->write(ptr, payload_length - worker_data_payload_header_length);

    return std::pair(packet, packet_length);
}
}
