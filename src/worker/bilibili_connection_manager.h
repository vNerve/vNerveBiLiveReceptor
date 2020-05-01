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

    std::unordered_map<int, bilibili_connection> _connections;

    boost::asio::ip::tcp::resolver _resolver;

    config::config_t _options;

    void on_resolved(const boost::system::error_code& err,
                     boost::asio::ip::tcp::resolver::iterator endpoint_iterator,
                     int room_id);
    void on_connected(
        const boost::system::error_code& err,
        boost::asio::ip::tcp::resolver::iterator endpoint_iterator,
        std::shared_ptr<boost::asio::ip::tcp::socket>, int);

    void on_room_failed(int room_id);
    void on_room_data(unsigned char* data, size_t len);

    std::string _shared_heartbeat_buffer_str;
    boost::asio::const_buffer _shared_heartbeat_buffer; // binary string :)

    boost::thread_specific_ptr<unsigned char> _shared_zlib_buffer;
    size_t _shared_zlib_buffer_size;

public:
    bilibili_connection_manager(config::config_t);
    ~bilibili_connection_manager();

    void open_connection(int room_id);

    const boost::asio::const_buffer& get_heartbeat_buffer()
    {
        return _shared_heartbeat_buffer;
    }

    boost::program_options::variables_map& get_options() { return *_options; }
    boost::asio::io_context& get_io_context() { return _context; }
    boost::asio::mutable_buffer get_shared_zlib_buffer();
};
} // namespace vNerve::bilibili
