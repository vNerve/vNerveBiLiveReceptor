#pragma once

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "config.h"
#include "bili_conn_plain_tcp.h"
#include "bili_conn_ws.h"

#include <memory>
#include <string>
#include <mutex>

namespace vNerve::bilibili
{
class borrowed_message;

using room_event_handler = std::function<void(int)>;
using room_data_handler = std::function<void(int, const borrowed_message*)>;

using enabled_bilibili_bilibili_connection = bilibili_connection_websocket;

///
/// Global network session for Bilibili Livestream chat crawling.
/// This should be created only once through the whole program.
class bilibili_connection_manager
{
    friend class bilibili_connection_plain_tcp;
    friend class bilibili_connection_websocket;
private:
    boost::asio::io_context _context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _guard;
    std::recursive_mutex _mutex;
    boost::thread_group _pool;

    std::unordered_map<int, std::shared_ptr<enabled_bilibili_bilibili_connection>> _connections;
    int _max_connections;

    room_event_handler _on_room_failed;
    room_data_handler _on_room_data;

    config::config_t _options;

    void on_room_failed(int room_id) { _on_room_failed(room_id); }
    void on_room_data(int room_id, const borrowed_message* msg) { _on_room_data(room_id, msg); }
    /// called on a room normally closes (usually by an unassignment)
    void on_room_closed(int room_id);

    std::string _shared_heartbeat_buffer_str;
    boost::asio::const_buffer _shared_heartbeat_buffer; // binary string :)

public:
    bilibili_connection_manager(config::config_t, room_event_handler on_room_failed, room_data_handler on_room_data);
    ~bilibili_connection_manager();

    void open_connection(int room_id);
    void close_connection(int room_id);
    void close_all_connections();

    const boost::asio::const_buffer& get_heartbeat_buffer()
    {
        return _shared_heartbeat_buffer;
    }

    boost::program_options::variables_map& get_options() { return *_options; }
    config::config_t get_options_ptr() { return _options; }
    boost::asio::io_context& get_io_context() { return _context; }
};
} // namespace vNerve::bilibili
