#pragma once

#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "config.h"
#include "bili_conn.h"

#include <memory>
#include <string>

#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread.hpp>

namespace vNerve::bilibili
{
class borrowed_message;

using room_event_handler = std::function<void(int)>;
using room_data_handler = std::function<void(int, borrowed_message*)>;

///
/// Global network session for Bilibili Livestream chat crawling.
/// This should be created only once through the whole program.
class bilibili_connection_manager : public std::enable_shared_from_this<bilibili_connection_manager>
{
    friend class bilibili_connection;
private:
    boost::asio::io_context _context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _guard;
    boost::thread_group _pool;
    boost::asio::ip::tcp::resolver _resolver;

    std::unordered_map<int, bilibili_connection> _connections;
    int _max_connections;

    room_event_handler _on_room_failed;
    room_data_handler _on_room_data;

    config::config_t _options;

    void on_resolved(const boost::system::error_code& err,
                     boost::asio::ip::tcp::resolver::iterator endpoint_iterator,
                     int room_id);
    void on_connected(
        const boost::system::error_code& err,
        std::shared_ptr<boost::asio::ip::tcp::socket>, int);

    void on_room_failed(int room_id) { _on_room_failed(room_id); }
    void on_room_data(int room_id, borrowed_message* msg) { _on_room_data(room_id, msg); }
    /// called on a room normally closes (usually by an unassignment)
    void on_room_closed(int room_id);

    std::string _shared_heartbeat_buffer_str;
    boost::asio::const_buffer _shared_heartbeat_buffer; // binary string :)

public:
    bilibili_connection_manager(config::config_t, room_event_handler on_room_failed, room_data_handler on_room_data);
    ~bilibili_connection_manager();

    void open_connection(int room_id);
    void close_connection(int room_id);

    const boost::asio::const_buffer& get_heartbeat_buffer()
    {
        return _shared_heartbeat_buffer;
    }

    boost::program_options::variables_map& get_options() { return *_options; }
    boost::asio::io_context& get_io_context() { return _context; }
};
} // namespace vNerve::bilibili
