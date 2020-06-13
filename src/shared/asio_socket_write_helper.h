#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <deque>

namespace vNerve::bilibili
{
using supervisor_buffer_deleter = std::function<void(unsigned char*)>;
using socket_close_handler = std::function<void()>;

class asio_socket_write_helper : public std::enable_shared_from_this<asio_socket_write_helper>
{
private:
    std::deque<std::tuple<unsigned char*, size_t, supervisor_buffer_deleter>> _write_queue;
    std::string _log_prefix;

    std::weak_ptr<boost::asio::ip::tcp::socket> _socket;
    socket_close_handler _close_handler;

    void start_async_write();
    void on_written(const boost::system::error_code& ec, size_t byte_transferred, int buffer_count);
    void delete_first_n_buffers(int n);

public:
    asio_socket_write_helper(std::string log_prefix, std::shared_ptr<boost::asio::ip::tcp::socket> socket, socket_close_handler close_handler);
    void reset(std::shared_ptr<boost::asio::ip::tcp::socket> socket);

    void write(unsigned char*, size_t, const supervisor_buffer_deleter&);


    asio_socket_write_helper(const asio_socket_write_helper& other) = delete;
    asio_socket_write_helper& operator=(const asio_socket_write_helper& other) = delete;

    asio_socket_write_helper(asio_socket_write_helper&& other) noexcept
        : _write_queue(std::move(other._write_queue)),
          _log_prefix(std::move(other._log_prefix)),
          _socket(std::move(other._socket)),
          _close_handler(std::move(other._close_handler))
    {
    }

    asio_socket_write_helper& operator=(asio_socket_write_helper&& other) noexcept
    {
        if (this == &other)
            return *this;
        _write_queue = std::move(other._write_queue);
        _log_prefix = std::move(other._log_prefix);
        _socket = std::move(other._socket);
        _close_handler = std::move(other._close_handler);
        return *this;
    }
};
}
