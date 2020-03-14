#include "bili_packet.h"

#include <boost/thread.hpp>

#include <zlib.h>
#include <iostream>
#include <cstdio> // for std::sprintf

namespace vNerve::bilibili
{
const size_t zlib_buffer_size = 256 * 1024;

std::pair<size_t, size_t> handle_buffer(unsigned char* buf, size_t transferred, size_t buffer_size, size_t skipping_size)
{
    if (skipping_size > transferred)
        return std::pair<size_t, size_t>(0, skipping_size - transferred); // continue disposing
    auto remaining = transferred - skipping_size;
    auto begin = buf + skipping_size;

    while (remaining > 0)
    {
        assert(remaining <= buffer_size && remaining <= transferred);
        if (remaining < sizeof(bilibili_packet_header))
        {
            // the remaining bytes can't even form a header, so move it to the head and wait for more data.
            std::memcpy(buf, begin, remaining);
            return std::pair<size_t, size_t>(remaining, 0);
        }
        auto header = reinterpret_cast<bilibili_packet_header*>(begin);
        auto length = header->length();
        if (header->header_length() != sizeof(bilibili_packet_header))
        {
            // TODO error: malformed packet!
            std::cerr << "Malformed packet!" << std::endl;
        }
        if (length > buffer_size)
        {
            std::cerr << "Disposing too big packet, size=" << length << std::endl;
            assert(header->length() > transferred);
            // The packet is too big, dispose it.
            // TODO log
            return std::pair<size_t, size_t>(0, header->length() - remaining); // skip the remaining bytes.
        }

        if (length > remaining)
        {
            // need more data.
            std::memcpy(buf, begin, remaining);
            return std::pair<size_t, size_t>(remaining, 0);
        }

        // 到此处我们拥有一个完整的数据包：[begin, begin + length)

        handle_packet(buf);
        remaining -= length;
    }

    return std::pair(0, 0); // read from starting, and skip no bytes.
}

boost::thread_specific_ptr<unsigned char> zlib_buffer = boost::thread_specific_ptr<unsigned char>([](unsigned char* buf) -> void { delete[] buf; });
unsigned char* get_zlib_buffer()
{
    if (!zlib_buffer.get())
        zlib_buffer.reset(new unsigned char[zlib_buffer_size]);
    return zlib_buffer.get();
}

unsigned char* decompress_buffer(unsigned char* buf, size_t size)
{
    auto zlib_buf = get_zlib_buffer();
    unsigned long out_size = zlib_buffer_size;
    auto result = uncompress(zlib_buf, &out_size, buf, size);
    switch (result)
    {
    case Z_OK: return zlib_buf;
    // TODO more explanation?
    }
    return nullptr;
}

void handle_packet(unsigned char* buf)
{
    auto header = reinterpret_cast<bilibili_packet_header*>(buf);
    if (header->header_length() != sizeof(bilibili_packet_header))
    {
        // TODO error: malformed packet!
    }

    auto payload_size = header->length() - sizeof(bilibili_packet_header);
    switch (header->protocol_version())
    {
    case zlib_compressed:
        {
        std::cerr << "Compressed message, decompressing" << std::endl;
        auto decompressed = decompress_buffer(buf + sizeof(bilibili_packet_header), payload_size);
        if (!decompressed)
        {
            // TODO malformed zlib data
        }
        handle_packet(decompressed);
        }
        break;
    default:
        switch (header->op_code())
        {
        case json_message:
            {
            auto json = std::string_view(reinterpret_cast<const char*>(buf + sizeof(bilibili_packet_header)), payload_size);
            // TODO parse and send
            std::cerr << "Received json." << std::endl;
            }
            break;
        case heartbeat_resp:
            {
            if (payload_size != sizeof(uint32_t))
            {
                // TODO malformed packet
            }
            auto popularity = boost::asio::detail::socket_ops::network_to_host_long(*reinterpret_cast<uint32_t*>(buf + sizeof(bilibili_packet_header)));
            std::cerr << "Popularity: " << popularity << std::endl;
            break;
            // TODO notification?
            }
        case join_room_resp:
            // TODO notification?
            std::cerr << "Joined room!" << std::endl;
            break;
        default:
            std::cerr << "Unknown packet!" << std::endl;
            // TODO unknown packet
            break;
        }
    }
}

std::string generate_heartbeat_packet()
{
    auto header = bilibili_packet_header();
    header.length(header.header_length());
    header.protocol_version(json_protocol);
    header.op_code(heartbeat);
    return std::string(reinterpret_cast<char*>(&header), sizeof(bilibili_packet_header));
}

const char* join_room_json_fmt = "{\"clientver\": \"1.6.3\",\"platform\": \"web\",\"protover\": %d,\"roomid\": %d,\"uid\": 0,\"type\": 2}";
const size_t join_room_json_max_length = 128;
std::string generate_join_room_packet(int room_id, int proto_ver)
{
    char payload[join_room_json_max_length];
    sprintf_s(payload, join_room_json_fmt, proto_ver, room_id);
    size_t payload_size = strnlen(payload, join_room_json_max_length);
    auto header = bilibili_packet_header();
    header.length(header.header_length() + payload_size);
    header.protocol_version(json_protocol);
    header.op_code(join_room);
    return std::string(reinterpret_cast<char*>(&header), sizeof(bilibili_packet_header))
        + payload;
}

}
