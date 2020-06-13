#pragma once
#include <memory>
#include <boost/asio/ip/tcp.hpp>

#include "simple_worker_proto.h"

namespace vNerve::bilibili::worker_supervisor
{
using socket_close_handler = std::function<void()>;

class simple_worker_proto_handler : public std::enable_shared_from_this<simple_worker_proto_handler>
{
private:
    std::string _log_prefix;

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
    simple_worker_proto_handler(std::string log_prefix, std::shared_ptr<boost::asio::ip::tcp::socket> socket, size_t buffer_size, buffer_handler buffer_handler, socket_close_handler close_handler);
    ~simple_worker_proto_handler();

    void start();
    void reset(std::shared_ptr<boost::asio::ip::tcp::socket> socket);


    simple_worker_proto_handler(const simple_worker_proto_handler& other) = delete;
    simple_worker_proto_handler& operator=(const simple_worker_proto_handler& other) = delete;

    simple_worker_proto_handler(simple_worker_proto_handler&& other) noexcept
        : _log_prefix(std::move(other._log_prefix)),
          _read_buffer_ptr(std::move(other._read_buffer_ptr)),
          _read_buffer_size(other._read_buffer_size),
          _read_buffer_offset(other._read_buffer_offset),
          _skipping_bytes(other._skipping_bytes),
          _socket(std::move(other._socket)),
          _close_handler(std::move(other._close_handler)),
          _buffer_handler(std::move(other._buffer_handler))
    {
    }

    simple_worker_proto_handler& operator=(simple_worker_proto_handler&& other) noexcept
    {
        if (this == &other)
            return *this;
        _log_prefix = std::move(other._log_prefix);
        _read_buffer_ptr = std::move(other._read_buffer_ptr);
        _read_buffer_size = other._read_buffer_size;
        _read_buffer_offset = other._read_buffer_offset;
        _skipping_bytes = other._skipping_bytes;
        _socket = std::move(other._socket);
        _close_handler = std::move(other._close_handler);
        _buffer_handler = std::move(other._buffer_handler);
        return *this;
    }
};

}

