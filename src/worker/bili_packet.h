#pragma once

#include <cstdint>

#include <boost/asio.hpp>

namespace vNerve::bilibili
{
class malformed_packet : public std::exception
{
public:
    malformed_packet() = default;
    explicit malformed_packet(char const* _Message) : exception(_Message) {}
    malformed_packet(char const* _Message, int i) : exception(_Message, i) {}
    explicit malformed_packet(exception const& _Other) : exception(_Other) {}
    malformed_packet(const malformed_packet& other) : std::exception(other) {}
    malformed_packet(malformed_packet&& other) noexcept
        : std::exception(std::move(other))
    {
    }

    malformed_packet& operator=(const malformed_packet& other)
    {
        if (this == &other)
            return *this;
        std::exception::operator=(other);
        return *this;
    }

    malformed_packet& operator=(malformed_packet&& other) noexcept
    {
        if (this == &other)
            return *this;
        std::exception::operator=(std::move(other));
        return *this;
    }
};

struct bilibili_packet_header
{
    uint32_t _length;
    uint16_t _header_length;
    uint16_t _protocol_version;
    uint32_t _op_code;
    uint32_t _sequence_id;
#define PACKET_ACCESSOR(name, type1, type2)                                  \
    inline type1 name() const                                                \
    {                                                                        \
        return boost::asio::detail::socket_ops::network_to_host_##type2(     \
            _##name##);                                                      \
    }                                                                        \
    inline void name(type1 value)                                            \
    {                                                                        \
        _##name## =                                                          \
            boost::asio::detail::socket_ops::host_to_network_##type2(value); \
    }
#define PACKET_ACCESSOR_LONG(name) PACKET_ACCESSOR(name, uint32_t, long)
#define PACKET_ACCESSOR_SHORT(name) PACKET_ACCESSOR(name, uint16_t, short)

    PACKET_ACCESSOR_LONG(length)
    PACKET_ACCESSOR_SHORT(header_length)
    PACKET_ACCESSOR_SHORT(protocol_version)
    PACKET_ACCESSOR_LONG(op_code)
    PACKET_ACCESSOR_LONG(sequence_id)

#undef PACKET_ACCESSOR_LONG
#undef PACKET_ACCESSOR_SHORT
#undef PACKET_ACCESSOR

    bilibili_packet_header()
    {
        header_length(sizeof(bilibili_packet_header));
        sequence_id(1);
    }
};

///
/// 用于处理一次读取获得的缓冲区。
/// 一次缓冲区可能不完整或包含多个数据包。本函数可以处理此种情况。
/// 本函数断言 *buf* 的最开始为一个完整的数据包头部。
/// @param buf *整个*缓冲区
/// @param transferred 本次读取到的字节数
/// @param buffer_size 整个缓冲区的大小
/// @param skipping_size 上次调用获得的返回值的第二项，标识应该跳过的大小
/// @return 下次读取结果应该存放的偏移量以及需要传入下一次调用最后一个参数的偏移量。如果本结果含有不完整的数据包，本函数将会将该数据包的一部分复制到 `buf` 开头，则返回的就是数据包片段的尾部位置 + 1.
std::pair<size_t, size_t> handle_buffer(unsigned char* buf, size_t transferred,
                                        size_t buffer_size,
                                        size_t skipping_size);

void handle_packet(unsigned char* buf);

std::string generate_heartbeat_packet();
std::string generate_join_room_packet(int room_id, int proto_ver);

enum bilibili_packet_op_code : uint32_t
{
    heartbeat = 2,
    heartbeat_resp = 3,
    json_message = 5,

    join_room = 7,
    join_room_resp = 8,
};

enum bilibili_packet_protocol_version : uint16_t
{
    json_protocol = 0,
    popularity = 1,
    zlib_compressed = 2,
};
}  // namespace vNerve::bilibili
