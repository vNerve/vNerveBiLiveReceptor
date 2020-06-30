#include "bili_packet.h"

#include "bili_json.h"
#include "borrowed_message.h"

#include <boost/thread.hpp>
#include <spdlog/spdlog.h>
#include <zlib.h>

#include <cstdio>  // for sprintf()

namespace vNerve::bilibili
{
const size_t zlib_buffer_size = 256 * 1024;

void handle_packet(unsigned char* buf, worker_supervisor::room_id_t, const message_handler&);

std::pair<size_t, size_t> handle_buffer(unsigned char* buf,
                                        const size_t transferred,
                                        const size_t buffer_size,
                                        const size_t last_remaining_size,
                                        const size_t skipping_size,
                                        worker_supervisor::room_id_t room_id,
                                        message_handler data_handler)
{
    SPDLOG_TRACE(
        "[bili_buffer] [{:p}] Handling buffer: transferred={}, buffer_size={}, skipping_size={}.",
        buf, transferred, buffer_size, skipping_size);
    if (skipping_size > transferred)
    {
        auto next_skipping_size = skipping_size - transferred;
        SPDLOG_TRACE(
            "[bili_buffer] [{:p}] Continue skipping message... Next skipping size=",
            buf, next_skipping_size);
        return std::pair<size_t, size_t>(
            0, next_skipping_size);  // continue disposing
    }
    long long remaining = transferred - skipping_size + last_remaining_size;
    auto begin = buf + skipping_size;

    while (remaining > 0)
    {
        SPDLOG_TRACE("[bili_buffer] [{:p}] Decoding message, remaining={}",
                      buf, remaining);
        assert(remaining <= buffer_size && remaining <= transferred);
        if (remaining < sizeof(bilibili_packet_header))
        {
            // the remaining bytes can't even form a header, so move it to the head and wait for more data.
            SPDLOG_TRACE(
                "[bili_buffer] [{:p}] Remaining bytes can't form a header. Request for more data be written to buf+{}.",
                buf, remaining);
            std::memmove(buf, begin, remaining);
            return std::pair<size_t, size_t>(remaining, 0);
        }
        auto header = reinterpret_cast<bilibili_packet_header*>(begin);
        auto length = header->length();
        auto header_length = header->header_length();
        if (header_length != sizeof(bilibili_packet_header))
        {
            spdlog::warn(
                "[bili_buffer] [{:p}] Malformed packet: Bad header length(!=16): {}",
                buf, header_length);
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
            std::memmove(buf, begin, remaining);
            SPDLOG_TRACE(
                "[bili_buffer] [{:p}] Packet not complete. Request for more data be written to buf+{}.",
                buf, remaining);
            return std::pair<size_t, size_t>(remaining, 0);
        }

        // 到此处我们拥有一个完整的数据包：[begin, begin + length)

        handle_packet(begin, room_id, data_handler);
        remaining -= length;
        begin += length;
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
    SPDLOG_TRACE("[zlib] [{:p}] Packet decompressed to {:p}. code={}, size={}",
                  buf, zlib_buf, result, out_size);
    return {result == Z_OK ? zlib_buf : nullptr, result, out_size};
}

void handle_packet(unsigned char* buf, worker_supervisor::room_id_t room_id, const message_handler& handler)
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
    //SPDLOG_TRACE(
    //    "[packet] [{:p}] Packet header: len={}, proto_ver={}, op_code={}, seq_id={}",
    //    buf, header->length(), header->protocol_version(),
    //    header->op_code(), header->sequence_id());

    switch (header->protocol_version())
    {
    case zlib_compressed:
    {
        SPDLOG_TRACE("[packet] [{:p}] Decompressing zlib-zipped packet.", buf);
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
        handle_buffer(decompressed, out_size, out_size, 0, 0, room_id, handler);
        //handle_packet(decompressed);
    }
    break;
    default:
        switch (header->op_code())
        {
        case json_message:
        {
            SPDLOG_TRACE("[packet] [{:p}] Received JSON data. len={}",
                          buf, payload_size);

            auto last_char_iter = reinterpret_cast<char*>(buf + sizeof(bilibili_packet_header) + payload_size);
            auto last_char = *last_char_iter;
            *last_char_iter = '\0';
            const borrowed_message* msg = serialize_buffer(reinterpret_cast<char*>(buf + sizeof(bilibili_packet_header)), payload_size, room_id);
            *last_char_iter = last_char;
            if (msg)
                handler(msg);
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
            SPDLOG_TRACE("[packet] [{:p}] Heartbeat response: Popularity={}",
                          buf, popularity);
            const borrowed_message* msg = serialize_popularity(popularity, room_id);
            handler(msg);
            break;
        }
        case join_room_resp:
            SPDLOG_TRACE("[packet] [{:p}] Successfully joined room.", buf);
            break;
        default:
            spdlog::warn("[packet] [{:p}] Unknown packet type! op_code={}",
                         buf, header->op_code());
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
    R"({"clientver":"1.11.0","platform":"web","protover":%d,"roomid":%d,"uid":0,"type":2,"key":"%s"})";
const size_t join_room_json_max_length = 256;
std::string generate_join_room_packet(int room_id, int proto_ver, std::string_view token)
{
    char payload[join_room_json_max_length];
    size_t payload_size = snprintf(payload, join_room_json_max_length, join_room_json_fmt, proto_ver, room_id, token.data());
    auto header = bilibili_packet_header();
    header.length(header.header_length() + payload_size);
    header.protocol_version(json_protocol);
    header.op_code(join_room);
    return std::string(reinterpret_cast<char*>(&header),
                       sizeof(bilibili_packet_header))
           + payload;
}

}  // namespace vNerve::bilibili
