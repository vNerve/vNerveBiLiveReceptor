#include "bilibili_connection_manager.h"

#include "bili_packet.h"

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/algorithm/algorithm.hpp>
#include <utility>
#include <spdlog/spdlog.h>

vNerve::bilibili::bilibili_connection_manager::bilibili_connection_manager(const config::config_t options, room_event_handler on_room_failed, room_data_handler on_room_data)
    : _context((*options)["threads"].as<int>()),
      _guard(_context.get_executor()),
      _max_connections((*options)["max-rooms"].as<int>()),
      _on_room_failed(std::move(on_room_failed)),
      _on_room_data(std::move(on_room_data)),
      _options(options),
      _shared_heartbeat_buffer_str(generate_heartbeat_packet()),
      _shared_heartbeat_buffer(
          boost::asio::buffer(_shared_heartbeat_buffer_str))
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
        spdlog::critical(
            "[session] Failed shutting down session IO Context! err:{}:{}:{}",
            ex.code().value(), ex.code().message(), ex.what());
    }
}

void vNerve::bilibili::bilibili_connection_manager::open_connection(const int room_id)
{
    spdlog::info("[session] Connecting room {}", room_id);
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    auto existing_iter = _connections.find(room_id);
    if (existing_iter != _connections.end() && existing_iter->second->closed())
    {
        existing_iter->second->close();
        _connections.erase(existing_iter);
    }
    auto [iter, inserted] = _connections.emplace(room_id, std::make_shared<enabled_bilibili_bilibili_connection>(this, room_id));
    if (inserted)
        iter->second->init();
}

void vNerve::bilibili::bilibili_connection_manager::close_connection(int room_id)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    spdlog::info("[session] Disconnecting room {}", room_id);
    auto iter = _connections.find(room_id);
    if (iter == _connections.end())
    {
        spdlog::debug("[session] Room {} not found.", room_id);
        return;
    }

    iter->second->close();
}

void vNerve::bilibili::bilibili_connection_manager::close_all_connections()
{
    post(_context.get_executor(), [this]() -> void {
        spdlog::info("[session] Disconnecting all rooms.");
        while (!_connections.empty())
            _connections.begin()->second->close(false);
    });
}

void vNerve::bilibili::bilibili_connection_manager::on_room_closed(int room_id)
{
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _connections.erase(room_id);
}
