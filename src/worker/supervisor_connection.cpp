#include "supervisor_connection.h"
#include <utility>

namespace vNerve::bilibili::worker_supervisor
{
supervisor_connection::supervisor_connection(const config::config_t config,
                                       const supervisor_buffer_handler buffer_handler)
    : _config(config),
      _guard(_context.get_executor()),
      _resolver(_context),
      _timer(_context),
      _retry_interval_sec((*config)["retry-interval-sec"].as<int>()),
      _supervisor_host((*config)["supervisor-host"].as<std::string>()),
      _supervisor_port(std::to_string((*config)["supervisor-port"].as<int>())),
      _buffer_handler(buffer_handler)
{
    _thread = boost::thread(boost::bind(&boost::asio::io_context::run, &_context));
}

supervisor_connection::~supervisor_connection()
{
    _guard.reset();
    _context.stop();
    // todo exception handling
}

void supervisor_connection::publish_msg(unsigned char* msg, size_t len,
                                        supervisor_buffer_deleter deleter)
{
    if (!_socket)
    {
        deleter(msg); // Dispose data.
        return;
    }
    _queue.enqueue(std::make_pair(boost::asio::buffer(msg, len), deleter));
    post(_context,
                      boost::bind(&supervisor_connection::start_async_write,
                                  shared_from_this()));
}

void supervisor_connection::start_async_write()
{
    if (!_socket || _pending_write)
        return;
    _pending_write = true;

    std::vector<boost::asio::mutable_buffer> buffers;
    auto buffers_with_deleter = new std::vector<supervisor_buffer_owned>();
    supervisor_buffer_owned buf;
    while (_queue.try_dequeue(buf))
        buffers_with_deleter->push_back(buf);
    if (buffers.empty())
    {
        _pending_write = false;
        return;
    }
    for (auto& buffer_with_deleter : *buffers_with_deleter)
        buffers.emplace_back(buffer_with_deleter.first);

    async_write(
        *_socket, buffers,
        boost::bind(&supervisor_connection::on_written, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred,
                    buffers_with_deleter));
}

void supervisor_connection::connect()
{
    if (_socket)
    {
        // TODO exception handling
        _socket->close();
        _socket.reset();
    }
    _socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    _resolver.async_resolve(
        _supervisor_host, _supervisor_port,
        boost::bind(&supervisor_connection::on_resolved, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::iterator));
}

void supervisor_connection::on_written(const boost::system::error_code& ec,
    size_t bytes_transferred, std::vector<supervisor_buffer_owned>* buffers)
{
    // First delete all buffers and the vector itself!!
    for (auto& buffer : *buffers)
        buffer.second(reinterpret_cast<unsigned char*>(buffer.first.data()));
    delete buffers;

    if (ec)
    {
        // TODO error handling and closing socket!
        _socket->close();
        _socket.reset();
        // TODO retry handling?
    }
    // todo log?
}

void supervisor_connection::on_resolved(const boost::system::error_code& ec,
    const boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{

}
}
