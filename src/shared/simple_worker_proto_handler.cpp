#include "simple_worker_proto_handler.h"

#include <spdlog/spdlog.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

namespace vNerve::bilibili::worker_supervisor
{

simple_worker_proto_handler::simple_worker_proto_handler(std::shared_ptr<boost::asio::ip::tcp::socket> socket, size_t buffer_size, buffer_handler buffer_handler, socket_close_handler close_handler)
    : _read_buffer_ptr(new unsigned char[buffer_size]),
      _read_buffer_size(buffer_size),
      _socket(socket),
      _close_handler(close_handler),
      _buffer_handler(buffer_handler)

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
        spdlog::trace(
            "[simple_message] Current socket invalidated! Closing.");
        return;
    }
    spdlog::trace(
        "[simple_message] Starting next async read. offset={}, size={}/{}", _read_buffer_offset, _read_buffer_size - _read_buffer_offset, _read_buffer_size);
    socket->async_receive(
        boost::asio::buffer(_read_buffer_ptr.get() + _read_buffer_offset,
                            _read_buffer_size - _read_buffer_offset),
        boost::bind(&simple_worker_proto_handler::on_receive, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void simple_worker_proto_handler::on_receive(const boost::system::error_code& ec, size_t transferred)
{
    if (ec)
    {
        if (ec.value() == boost::asio::error::operation_aborted)
            spdlog::debug("[simple_message] Cancelling async reading."); // closing socket.
        else
            _close_handler();
        _socket = std::shared_ptr<boost::asio::ip::tcp::socket>(nullptr);
        return;
    }

    spdlog::debug("[simple_message] Received data block(len={})", transferred);
    auto [new_offset, new_skipping_bytes] =
        handle_simple_message(_read_buffer_ptr.get(), transferred, _read_buffer_size,
                              _skipping_bytes, _buffer_handler);
    _read_buffer_offset = new_offset;
    _skipping_bytes = new_skipping_bytes;

    start_async_read();
}
}
