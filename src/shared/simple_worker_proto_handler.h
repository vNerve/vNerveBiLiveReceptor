#pragma once
#include <memory>
#include <boost/asio/ip/tcp.hpp>

#include "simple_worker_proto.h"

namespace vNerve::bilibili::worker_supervisor
{
using socket_close_handler = std::function<void()>;

class simple_worker_proto_handler
{
private:
    std::unique_ptr<unsigned char[]> _read_buffer_ptr;
    size_t _read_buffer_size;
    size_t _read_buffer_offset = 0;
    size_t _skipping_bytes = 0;

    std::weak_ptr<boost::asio::ip::tcp::socket> _socket;
    socket_close_handler _close_handler;
    buffer_handler _buffer_handler;

    void start_async_read();
    void on_receive(const boost::system::error_code& ec, size_t transferred);

public:
    simple_worker_proto_handler(std::shared_ptr<boost::asio::ip::tcp::socket> socket, size_t buffer_size, buffer_handler buffer_handler, socket_close_handler close_handler);
    void reset(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
};

}

