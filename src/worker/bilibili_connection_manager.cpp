#include "bilibili_connection_manager.h"

#include "bili_packet.h"

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <spdlog/spdlog.h>

vNerve::bilibili::bilibili_connection_manager::bilibili_connection_manager(const config::config_t options)
    : _context((*_options)["threads"].as<int>()),
      _guard(_context.get_executor()),
      _max_connections((*_options)["max-rooms"].as<int>()),
      _resolver(_context),
      _options(options),
      _shared_heartbeat_buffer_str(generate_heartbeat_packet()),
      _shared_heartbeat_buffer(
          boost::asio::buffer(_shared_heartbeat_buffer_str)),
      _shared_zlib_buffer([](unsigned char* buf) -> void {
          delete[] buf;
      }),
      _shared_zlib_buffer_size((*_options)["zlib-buffer"].as<size_t>())
{
    int threads = (*_options)["threads"].as<int>();
    spdlog::info("[session] Creating session with thread pool size={}",
                 threads);
    for (int i = 0; i < threads; i++)
        _pool.create_thread(
            boost::bind(&boost::asio::io_context::run, &_context));
}

vNerve::bilibili::bilibili_connection_manager::~bilibili_connection_manager()
{
    try
    {
        _context.stop();
    }
    catch (boost::system::system_error& ex)
    {
        spdlog::error(
            "[session] Failed shutting down session IO Context! err:{}:{}:{}",
            ex.code().value(), ex.code().message(), ex.what());
    }
}

boost::asio::mutable_buffer
vNerve::bilibili::bilibili_connection_manager::get_shared_zlib_buffer()
{
    if (!_shared_zlib_buffer.get())
        _shared_zlib_buffer.reset(new unsigned char[_shared_zlib_buffer_size]);
    return boost::asio::buffer(_shared_zlib_buffer.get(),
                               _shared_zlib_buffer_size);
}

void vNerve::bilibili::bilibili_connection_manager::open_connection(const int room_id)
{
    auto& server_addr = (*_options)["chat-server"].as<std::string>();
    auto port = std::to_string((*_options)["chat-server-port"].as<int>());

    spdlog::info("[session] Connecting room {}", room_id);
    spdlog::debug(
        "[session] Connecting room {} with server {}:{}, resolving DN.",
        room_id, server_addr, port);
    _resolver.async_resolve(
        server_addr, port,
        boost::bind(&bilibili_connection_manager::on_resolved, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::iterator, room_id));
}

void vNerve::bilibili::bilibili_connection_manager::close_connection(int room_id)
{
    spdlog::info("[session] Disconnecting room {}", room_id);
    auto iter = _connections.find(room_id);
    if (iter == _connections.end())
    {
        spdlog::debug("[session] Room {} not found.", room_id);
        return;
    }

    iter->second.close();
}

void vNerve::bilibili::bilibili_connection_manager::on_resolved(
    const boost::system::error_code& err,
    const boost::asio::ip::tcp::resolver::iterator endpoint_iterator,
    const int room_id)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug(
                "[session] Cancelling connecting(resolving) to room {}.",
                room_id);
            return;
        }
        spdlog::warn(
            "[session] Failed resolving DN connecting to room {}! err: {}:{}",
            room_id, err.value(), err.message());
        on_room_failed(room_id);
        return;
    }

    spdlog::debug(
        "[session] Connecting room {}: server DN resolved, connecting to endpoints.",
        room_id);
    auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_context);
    async_connect(
        *socket, endpoint_iterator,
        boost::bind(&bilibili_connection_manager::on_connected, this,
                    boost::asio::placeholders::error, socket, room_id));
}

void vNerve::bilibili::bilibili_connection_manager::on_connected(
    const boost::system::error_code& err,
    std::shared_ptr<boost::asio::ip::tcp::socket> socket, int room_id)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            spdlog::debug("[session] Cancelling connecting to room {}.",
                          room_id);
            return;
        }
        spdlog::warn("[session] Failed connecting to room {}! err: {}:{}",
                     room_id, err.value(), err.message());
        on_room_failed(room_id);
        return;
    }

    spdlog::debug("[session] Connected to room {}. Setting up connection protocol.", room_id);
    _connections.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(room_id),
        std::forward_as_tuple(socket, shared_from_this(), room_id)); // Construct connection obj.
}

void vNerve::bilibili::bilibili_connection_manager::on_room_failed(int room_id)
{
    // TODO impl
}

void vNerve::bilibili::bilibili_connection_manager::on_room_data(int room_id, borrowed_message* msg)
{
    // TODO impl
}

void vNerve::bilibili::bilibili_connection_manager::on_room_closed(int room_id)
{
    _connections.erase(room_id);
}
