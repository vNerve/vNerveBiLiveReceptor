#include "simple_worker_proto.h"

#include <spdlog/spdlog.h>
#include <boost/asio/detail/socket_ops.hpp>

#include <cstring>

std::pair<size_t, size_t> vNerve::bilibili::worker_supervisor::handle_simple_message(unsigned char* buf, size_t transferred, size_t buffer_size, size_t skipping_size, buffer_handler handler)
{
    using namespace boost::asio::detail::socket_ops;
    spdlog::trace(
        "[simple_message] [{:p}] Handling buffer: transferred={}, buffer_size={}, skipping_size={}.",
        buf, transferred, buffer_size, skipping_size);
    if (skipping_size > transferred)
    {
        auto next_skipping_size = skipping_size - transferred;
        spdlog::trace(
            "[simple_message] [{:p}] Continue skipping message... Next skipping size=",
            buf, next_skipping_size);
        return std::pair<size_t, size_t>(
            0, next_skipping_size);  // continue disposing
    }
    long long remaining = transferred - skipping_size;
    auto begin = buf + skipping_size;

    while (remaining > 0)
    {
        spdlog::trace("[simple_message] [{:p}] Decoding message, remaining={}",
                      buf, remaining);
        assert(remaining <= buffer_size && remaining <= transferred);
        if (remaining < simple_message_header_length)
        {
            // the remaining bytes can't even form a header, so move it to the head and wait for more data.
            spdlog::trace(
                "[simple_message] [{:p}] Remaining bytes can't form a header. Request for more data be written to buf+{}.",
                buf, remaining);
            std::memmove(buf, begin, remaining);
            return std::pair<size_t, size_t>(remaining, 0);
        }
        auto length = network_to_host_long(*reinterpret_cast<simple_message_header*>(begin));
        if (length > buffer_size)
        {
            spdlog::info(
                "[simple_message] [{:p}] Packet too big: {} > max size({}). Disposing and skipping next {} bytes.",
                buf, length, buffer_size, length - remaining);
            assert(length > transferred);
            // The packet is too big, dispose it.
            return std::pair<size_t, size_t>(
                0, length - remaining);  // skip the remaining bytes.
        }

        if (length > remaining)
        {
            // need more data.
            std::memmove(buf, begin, remaining);
            spdlog::trace(
                "[simple_message] [{:p}] Packet not complete. Request for more data be written to buf+{}.",
                buf, remaining);
            return std::pair<size_t, size_t>(remaining, 0);
        }

        // 到此处我们拥有一个完整的数据包：[begin, begin + length)

        handler(begin + simple_message_header_length, length);
        remaining -= (length + simple_message_header_length);
        begin += (length + simple_message_header_length);
    }

    return std::pair(0, 0);  // read from starting, and skip no bytes.
}
