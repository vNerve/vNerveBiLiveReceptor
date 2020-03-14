#include "bili_session.h"

#include "bili_packet.h"

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include <spdlog/spdlog.h>

vNerve::bilibili::bilibili_session::bilibili_session(const std::shared_ptr<boost::program_options::variables_map> options)
: _guard(_context.get_executor()),
_resolver(_context),
_options(options),
_shared_heartbeat_buffer_str(generate_heartbeat_packet()),
_shared_heartbeat_buffer(boost::asio::buffer(_shared_heartbeat_buffer_str)),
_shared_zlib_buffer([](unsigned char* buf) -> void { delete[] buf; }),
_shared_zlib_buffer_size((*_options)["zlib-buffer"].as<size_t>())
{
    int threads = (*_options)["threads"].as<int>();
    spdlog::info("[session] Creating session with thread pool size={}", threads);
    for (int i = 0; i < threads; i++)
        _pool.create_thread(boost::bind(&boost::asio::io_context::run, &_context));
}

vNerve::bilibili::bilibili_session::~bilibili_session()
{
    // TODO Exception handling
    try
    {
        _context.stop();
    }
    catch (boost::system::system_error& ex)
    {
        spdlog::error("[session] Failed shutting down session IO Context! err:{}:{}:{}", ex.code().value(), ex.code().message(), ex.what());
    }
}

boost::asio::mutable_buffer vNerve::bilibili::bilibili_session::get_shared_zlib_buffer()
{
    if (!_shared_zlib_buffer.get())
        _shared_zlib_buffer.reset(new unsigned char[_shared_zlib_buffer_size]);
    return boost::asio::buffer(_shared_zlib_buffer.get(), _shared_zlib_buffer_size);
}

void vNerve::bilibili::bilibili_session::open_connection(const int room_id)
{
    auto& server_addr = (*_options)["chat-server"].as<std::string>();
    auto port = std::to_string((*_options)["chat-server-port"].as<int>());

    spdlog::info("[session] Connecting room {}", room_id);
    spdlog::debug("[session] Connecting room {} with server {}:{}, resolving DN.", room_id, server_addr, port);
    _resolver.async_resolve(
        server_addr, port,
        boost::bind(&bilibili_session::on_resolved, this, boost::asio::placeholders::error, boost::asio::placeholders::iterator, room_id));
}

void vNerve::bilibili::bilibili_session::on_resolved(
    const boost::system::error_code& err,
    const boost::asio::ip::tcp::resolver::iterator endpoint_iterator,
    const int room_id)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[session] Cancelling connecting(resolving) to room {}.", room_id);
            return;
        }
        // TODO error handling
        spdlog::warn("[session] Failed resolving DN connecting to room {}! err: {}:{}", room_id, err.value(), err.message());
        return;
    }

    spdlog::debug("[session] Connecting room {}: server DN resolved, connecting to endpoints.", room_id);
    std::shared_ptr<boost::asio::ip::tcp::socket> socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    async_connect(
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
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[session] Cancelling connecting to room {}.", room_id);
            return;
        }
        spdlog::warn("[session] Failed connecting to room {}! err: {}:{}", room_id, err.value(), err.message());
        return;
    }

    spdlog::debug("[session] Connected to room {}. Setting up connection protocol.", room_id);
    _connections.emplace_back(socket, shared_from_this(), room_id);
    // TODO notification?
}
