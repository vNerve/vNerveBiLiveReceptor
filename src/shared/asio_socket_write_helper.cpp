#include "asio_socket_write_helper.h"
#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <utility>

#define LOG_PREFIX "[a_sock] "

namespace vNerve::bilibili
{

asio_socket_write_helper::asio_socket_write_helper(std::string log_prefix, std::shared_ptr<boost::asio::ip::tcp::socket> socket, socket_close_handler close_handler)
    : _log_prefix(std::move(log_prefix)), _socket(socket), _close_handler(std::move(close_handler))
{
}

void asio_socket_write_helper::start_async_write()
{
    auto socket = _socket.lock();
    if (!socket)
    {
        SPDLOG_DEBUG(
            LOG_PREFIX "Current socket invalidated! Closing.");
        return;
    }
    int count = static_cast<int>(_write_queue.size());
    if (count > 1)
    {
        SPDLOG_TRACE(LOG_PREFIX "{} Starting batch async write. BufferCount={}", _log_prefix, count);
        std::vector<boost::asio::const_buffer> buffers(count);
        for (int i = 0; i < count; i++)
        {
            auto& buf_iter = _write_queue[i];
            SPDLOG_TRACE(LOG_PREFIX "{} Buffer #{}: Len={}", _log_prefix, i, std::get<1>(buf_iter));
            buffers.emplace_back(std::get<0>(buf_iter), std::get<1>(buf_iter));
        }
        async_write(
            *socket,
            buffers,
            boost::bind(&asio_socket_write_helper::on_written, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred,
                        count));
    }
    else
    {
        // Single buffer: avoid vector allocating.
        auto& buf_iter = _write_queue.front();
        SPDLOG_TRACE(LOG_PREFIX "{} Starting async write. Len={}", _log_prefix, std::get<1>(buf_iter));
        async_write(
            *socket,
            boost::asio::buffer(std::get<0>(buf_iter), std::get<1>(buf_iter)),
            boost::bind(&asio_socket_write_helper::on_written, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred,
                        1));
    }
}

void asio_socket_write_helper::on_written(const boost::system::error_code& ec, size_t byte_transferred, int buffer_count)
{
    buffer_count = std::min(buffer_count, static_cast<int>(_write_queue.size()));
    for (int i = 0; i < buffer_count; i++)
    {
        auto& buf_iter = _write_queue.front();
        std::get<2>(buf_iter)(std::get<0>(buf_iter));
        _write_queue.pop_front();
    }

    if (ec.value() == boost::asio::error::operation_aborted)
        return;
    if (ec)
    {
        spdlog::warn(LOG_PREFIX "{} Error writing to socket! Disconnecting. err: {}:{}", _log_prefix, ec.value(), ec.message());
        _close_handler();
    }
    SPDLOG_DEBUG(LOG_PREFIX "{} Written {} bytes in {} buffers.", _log_prefix, byte_transferred, buffer_count);
    if (!_write_queue.empty())
        start_async_write();
}

void asio_socket_write_helper::delete_first_n_buffers(int n)
{
    n = std::min(n, static_cast<int>(_write_queue.size()));
    for (int i = 0; i < n; i++)
    {
        auto& buf_iter = _write_queue.front();
        std::get<2>(buf_iter)(std::get<0>(buf_iter)); // Use deleter
        _write_queue.pop_front();
    }
}

void asio_socket_write_helper::reset(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    auto old_socket = _socket.lock();
    boost::system::error_code nec;
    if (old_socket)
        old_socket->cancel(nec);
    _socket = socket;

    delete_first_n_buffers(static_cast<int>(_write_queue.size()));
}

void asio_socket_write_helper::write(unsigned char* buf, size_t len, const supervisor_buffer_deleter& deleter)
{
    auto socket = _socket.lock();
    if (!socket)
    {
        deleter(buf);
        return;
    }
    post(socket->get_executor(), [=]() {
        if (!socket)
        {
            deleter(buf);
            return;
        }
        bool write_in_process = _write_queue.empty();
        _write_queue.emplace_back(buf, len, deleter);
        if (!write_in_process)
            start_async_write();
    });
}
}
