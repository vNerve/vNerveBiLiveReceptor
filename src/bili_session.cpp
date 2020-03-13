#include "bili_session.h"

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "bili_packet.h"
#include "bili_conn.h"

vNerve::bilibili::bilibili_session::bilibili_session(std::shared_ptr<boost::program_options::variables_map> options)
: _options(options), _guard(_context.get_executor()),
_resolver(_context),
_shared_heartbeat_buffer_str(generate_heartbeat_packet()),
_shared_heartbeat_buffer(boost::asio::buffer(_shared_heartbeat_buffer_str)),
_shared_zlib_buffer([](unsigned char* buf) -> void { delete[] buf; }),
_shared_zlib_buffer_size((*_options)["zlib-buffer"].as<size_t>())
{
    int threads = (*_options)["threads"].as<int>();
    for (int i = 0; i < threads; i++)
        _pool.create_thread(boost::bind(&boost::asio::io_context::run, &_context));
}

vNerve::bilibili::bilibili_session::~bilibili_session()
{
    // TODO Exception handling
    _context.stop();
}

boost::asio::mutable_buffer vNerve::bilibili::bilibili_session::get_shared_zlib_buffer()
{
    if (!_shared_zlib_buffer.get())
        _shared_zlib_buffer.reset(new unsigned char[_shared_zlib_buffer_size]);
    return boost::asio::buffer(_shared_zlib_buffer.get(), _shared_zlib_buffer_size);
}

void vNerve::bilibili::bilibili_session::open_connection(int room_id)
{
    _resolver.async_resolve(
        (*_options)["chat-server"].as<std::string>(),
        std::to_string((*_options)["chat-server-port"].as<int>()),
        boost::bind(&bilibili_session::on_resolved, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator, room_id));
}

void vNerve::bilibili::bilibili_session::on_resolved(
    const boost::system::error_code& err,
    boost::asio::ip::tcp::resolver::iterator endpoint_iterator,
    int room_id)
{
    if (err)
    {
        // TODO error handling
        return;
    }

    std::shared_ptr<boost::asio::ip::tcp::socket> socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    boost::asio::async_connect(
        *socket,
        endpoint_iterator,
        boost::bind(&bilibili_session::on_connected, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator, socket, room_id));
}

void vNerve::bilibili::bilibili_session::on_connected(
    const boost::system::error_code& err,
    boost::asio::ip::tcp::resolver::iterator endpoint_iterator,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket,
    int room_id)
{
    if (err)
    {
        // TODO error handling
        return;
    }

    _connections.emplace_back(socket, shared_from_this(), room_id);
    // TODO notification?
}
