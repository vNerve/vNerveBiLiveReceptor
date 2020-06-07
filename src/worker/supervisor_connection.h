#pragma once

#include "config.h"
#include "simple_worker_proto_handler.h"
#include "asio_socket_write_helper.h"

#include <boost/thread.hpp>
#include <boost/asio.hpp>

#include <deque>
#include <concurrentqueue.h>

namespace vNerve::bilibili::worker_supervisor
{
using supervisor_connected_handler = std::function<void()>;
using supervisor_buffer_handler = std::function<void(unsigned char*, size_t)>;
using supervisor_buffer_deleter = std::function<void(unsigned char*)>;
using supervisor_buffer_owned = std::pair<boost::asio::mutable_buffer, supervisor_buffer_deleter>;

inline const int MAX_WRITE_BATCH = 10;

class supervisor_connection : std::enable_shared_from_this<supervisor_connection>
{
private:
    config::config_t _config;

    boost::asio::io_context _context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _guard;

    // WARNING: Don't turn this into thread pool. Only single working thread is allowed.
    boost::thread _thread;
    boost::asio::ip::tcp::resolver _resolver;
    std::shared_ptr<boost::asio::ip::tcp::socket> _socket;

    simple_worker_proto_handler _proto_handler;
    asio_socket_write_helper _write_helper;

    boost::asio::deadline_timer _timer;
    int _retry_interval_sec;

    std::string _supervisor_host;
    std::string _supervisor_port;

    supervisor_connected_handler _connected_handler;

    void connect();
    void force_close();
    void reschedule_retry_timer();

    void on_retry_timer_tick(const boost::system::error_code& ec);
    void on_resolved(
        const boost::system::error_code& ec,
        boost::asio::ip::tcp::resolver::iterator endpoint_iterator
        );
    void on_connected(const boost::system::error_code& ec, std::shared_ptr<boost::asio::ip::tcp::socket> socket);
    void on_failed();

public:
    supervisor_connection(config::config_t config,
                          supervisor_buffer_handler buffer_handler, supervisor_connected_handler connected_handler);
    ~supervisor_connection();

    // Take the ownership of msg.
    void publish_msg(unsigned char* msg, size_t len, supervisor_buffer_deleter deleter);
};
}  // namespace vNerve::bilibili::live::worker_supervisor