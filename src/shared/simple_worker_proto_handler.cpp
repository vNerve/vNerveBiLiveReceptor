#include "simple_worker_proto_handler.h"

#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#define LOG_PREFIX "[simp_msg] "

namespace vNerve::bilibili::worker_supervisor
{
simple_worker_proto_handler::simple_worker_proto_handler(std::string log_prefix, std::shared_ptr<boost::asio::ip::tcp::socket> socket, size_t buffer_size, buffer_handler buffer_handler, socket_close_handler close_handler)
    : _log_prefix(log_prefix),
      _read_buffer_ptr(new unsigned char[buffer_size]),
      _read_buffer_size(buffer_size),
      _socket(socket),
      _close_handler(std::move(close_handler)),
      _buffer_handler(std::move(buffer_handler))

{
}

simple_worker_proto_handler::~simple_worker_proto_handler()
{
}

void simple_worker_proto_handler::start()
{
    start_async_read();
}

void simple_worker_proto_handler::reset(std::shared_ptr<boost::asio::ip::tcp::socket> socket)
{
    auto old_socket = _socket.lock();
    boost::system::error_code nec;
    if (old_socket)
        old_socket->cancel(nec);
    _socket = socket;
    start_async_read();
}

void simple_worker_proto_handler::start_async_read()
{
    auto socket = _socket.lock();
    if (!socket)
    {
        SPDLOG_TRACE(LOG_PREFIX "{} Current socket invalidated! Closing.", _log_prefix);
        return;
    }
    SPDLOG_TRACE(
        LOG_PREFIX "{} Starting next async read. offset={}, size={}/{}", _log_prefix, _read_buffer_offset, _read_buffer_size - _read_buffer_offset, _read_buffer_size);
    socket->async_receive(
        boost::asio::buffer(_read_buffer_ptr.get() + _read_buffer_offset,
                            _read_buffer_size - _read_buffer_offset),
        boost::bind(&simple_worker_proto_handler::on_receive, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void simple_worker_proto_handler::on_receive(const boost::system::error_code& ec, size_t transferred)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
            SPDLOG_DEBUG(LOG_PREFIX "Cancelling async reading.");  // closing socket.
        else
            _close_handler();
        _socket = std::shared_ptr<boost::asio::ip::tcp::socket>(nullptr);
        return;
    }

    SPDLOG_DEBUG(LOG_PREFIX "{} Received data block(len={})", _log_prefix, transferred);
    auto [new_offset, new_skipping_bytes] =
        handle_simple_message(_read_buffer_ptr.get(), transferred, _read_buffer_size,
                              _read_buffer_offset, _skipping_bytes, _buffer_handler);
    _read_buffer_offset = new_offset;
    _skipping_bytes = new_skipping_bytes;

    start_async_read();
}
}  // namespace vNerve::bilibili::worker_supervisor
