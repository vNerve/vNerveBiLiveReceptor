#include "bili_packet.h"

#include <cstdio>  // for sprintf()

#include <boost/thread.hpp>
#include <spdlog/spdlog.h>
#include <zlib.h>

namespace vNerve::bilibili
{
const size_t zlib_buffer_size = 256 * 1024;

std::pair<size_t, size_t> handle_buffer(unsigned char* buf,
                                        const size_t transferred,
                                        const size_t buffer_size,
                                        const size_t skipping_size)
{
    spdlog::trace(
        "[bili_buffer] [{:p}] Handling buffer: transferred={}, buffer_size={}, skipping_size={}.",
        buf, transferred, buffer_size, skipping_size);
    if (skipping_size > transferred)
    {
        auto next_skipping_size = skipping_size - transferred;
        spdlog::trace(
            "[bili_buffer] [{:p}] Continue skipping message... Next skipping size=",
            buf, next_skipping_size);
        return std::pair<size_t, size_t>(
            0, next_skipping_size);  // continue disposing
    }
    auto remaining = transferred - skipping_size;
    auto begin = buf + skipping_size;

    while (remaining > 0)
    {
        spdlog::trace("[bili_buffer] [{:p}] Decoding message, remaining={}",
                      buf, remaining);
        assert(remaining <= buffer_size && remaining <= transferred);
        if (remaining < sizeof(bilibili_packet_header))
        {
            // the remaining bytes can't even form a header, so move it to the head and wait for more data.
            spdlog::trace(
                "[bili_buffer] [{:p}] Remaining bytes can't form a header. Request for more data be written to buf+{}.",
                buf, remaining);
            std::memcpy(buf, begin, remaining);
            return std::pair<size_t, size_t>(remaining, 0);
        }
        auto header = reinterpret_cast<bilibili_packet_header*>(begin);
        auto length = header->length();
        if (header->header_length() != sizeof(bilibili_packet_header))
        {
            spdlog::warn(
                "[bili_buffer] [{:p}] Malformed packet: Bad header length(!=16): {}",
                buf, header->header_length());
            throw malformed_packet();
        }
        if (length > buffer_size)
        {
            spdlog::info(
                "[bili_buffer] [{:p}] Packet too big: {} > max size({}). Disposing and skipping next {} bytes.",
                buf, length, buffer_size, header->length() - remaining);
            assert(header->length() > transferred);
            // The packet is too big, dispose it.
            return std::pair<size_t, size_t>(
                0, header->length() - remaining);  // skip the remaining bytes.
        }

        if (length > remaining)
        {
            // need more data.
            std::memcpy(buf, begin, remaining);
            spdlog::trace(
                "[bili_buffer] [{:p}] Packet not complete. Request for more data be written to buf+{}.",
                buf, remaining);
            return std::pair<size_t, size_t>(remaining, 0);
        }

        // 到此处我们拥有一个完整的数据包：[begin, begin + length)

        handle_packet(buf);
        remaining -= length;
        buf += length;
    }

    return std::pair(0, 0);  // read from starting, and skip no bytes.
}

boost::thread_specific_ptr<unsigned char> zlib_buffer =
    boost::thread_specific_ptr<unsigned char>([](unsigned char* buf) -> void {
        delete[] buf;
    });
unsigned char* get_zlib_buffer()
{
    if (!zlib_buffer.get())
        zlib_buffer.reset(new unsigned char[zlib_buffer_size]);
    return zlib_buffer.get();
}

std::tuple<unsigned char*, int, unsigned long> decompress_buffer(
    unsigned char* buf, size_t size)
{
    auto zlib_buf = get_zlib_buffer();
    unsigned long out_size = zlib_buffer_size;
    auto result = uncompress(zlib_buf, &out_size, buf, size);
    spdlog::trace("[zlib] [{:p}] Packet decompressed to {:p}. code={}, size={}",
                  buf, zlib_buf, result, out_size);
    return {result == Z_OK ? zlib_buf : nullptr, result, out_size};
}

void handle_packet(unsigned char* buf)
{
    auto header = reinterpret_cast<bilibili_packet_header*>(buf);
    if (header->header_length() != sizeof(bilibili_packet_header))
    {
        spdlog::warn(
            "[packet] [{:p}] Malformed packet: Bad header length(!=16): {}",
            buf, header->header_length());
        throw malformed_packet();
    }

    auto payload_size = header->length() - sizeof(bilibili_packet_header);
    spdlog::trace(
        "[packet] [{:p}] Packet header: len={}, proto_ver={}, op_code={}, seq_id={}",
        buf, header->length(), header->protocol_version(), header->op_code(),
        header->sequence_id());

    switch (header->protocol_version())
    {
    case zlib_compressed:
    {
        spdlog::trace("[packet] [{:p}] Decompressing zlib-zipped packet.", buf);
        auto [decompressed, err_code, out_size] = decompress_buffer(
            buf + sizeof(bilibili_packet_header), payload_size);
        if (!decompressed)
        {
            switch (err_code)
            {
            case Z_BUF_ERROR:
                spdlog::warn(
                    "[packet] [{:p}] Failed decompressing zlib-zipped packet! Packet too big.",
                    buf);
                break;
            case Z_DATA_ERROR:
                spdlog::warn(
                    "[packet] [{:p}] Failed decompressing zlib-zipped packet! Malformed data.",
                    buf);
                break;
            default:
                spdlog::warn(
                    "[packet] [{:p}] Failed decompressing zlib-zipped packet! errno={}. Please refer to zlib documentation.",
                    buf, err_code);
            }
            return;
        }
        handle_buffer(decompressed, out_size, out_size, 0);
        //handle_packet(decompressed);
    }
    break;
    default:
        switch (header->op_code())
        {
        case json_message:
        {
            auto json =
                std::string_view(reinterpret_cast<const char*>(
                                     buf + sizeof(bilibili_packet_header)),
                                 payload_size);
            // TODO parse and send
            spdlog::trace("[packet] [{:p}] Received JSON data. len=", buf,
                          payload_size);
        }
        break;
        case heartbeat_resp:
        {
            if (payload_size != sizeof(uint32_t))
            {
                spdlog::warn(
                    "[packet] [{:p}] Malformed heartbeat response: Bad payload size(!=4): {}",
                    buf, payload_size);
                return;
            }
            auto popularity =
                boost::asio::detail::socket_ops::network_to_host_long(
                    *reinterpret_cast<uint32_t*>(
                        buf + sizeof(bilibili_packet_header)));
            spdlog::trace("[packet] [{:p}] Heartbeat response: Popularity={}",
                          buf, popularity);
            break;
            // TODO send
        }
        case join_room_resp:
            // TODO notification?
            spdlog::trace("[packet] [{:p}] Successfully joined room.", buf);
            break;
        default:
            spdlog::warn("[packet] [{:p}] Unknown packet type! op_code={}", buf,
                         header->op_code());
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
    return std::string(reinterpret_cast<char*>(&header),
                       sizeof(bilibili_packet_header));
}

const char* join_room_json_fmt =
    "{\"clientver\": \"1.6.3\",\"platform\": \"web\",\"protover\": %d,\"roomid\": %d,\"uid\": 0,\"type\": 2}";
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
    return std::string(reinterpret_cast<char*>(&header),
                       sizeof(bilibili_packet_header)) +
           payload;
}

}  // namespace vNerve::bilibili
