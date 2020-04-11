#pragma once

#include "config.h"
#include <boost/thread.hpp>
#include <boost/asio.hpp>

#include <deque>
#include <concurrentqueue.h>

namespace vNerve::bilibili::worker_supervisor
{
using supervisor_buffer_handler = std::function<void(unsigned char*, size_t)>;
using supervisor_buffer_deleter = std::function<void(unsigned char*)>;
using supervisor_buffer_owned = std::pair<boost::asio::mutable_buffer, supervisor_buffer_deleter>;

class supervisor_connection : std::enable_shared_from_this<supervisor_connection>
{
private:
    config::config_t _config;

    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _guard;
    boost::asio::io_context _context;

    // WARNING: Don't turn this into thread pool. Only single working thread is allowed.
    boost::thread _thread;
    boost::asio::ip::tcp::resolver _resolver;
    std::shared_ptr<boost::asio::ip::tcp::socket> _socket;

    boost::asio::deadline_timer _timer;
    int _retry_interval_sec;

    std::string _supervisor_host;
    std::string _supervisor_port;

    moodycamel::ConcurrentQueue<supervisor_buffer_owned> _queue;
    bool _pending_write = false;
    supervisor_buffer_handler _buffer_handler;

    void start_async_write();
    void connect();
    void on_written(const boost::system::error_code& ec,
                    size_t bytes_transferred,
                    std::vector<supervisor_buffer_owned>* buffers);
    void on_resolved(
        const boost::system::error_code& ec,
        const boost::asio::ip::tcp::resolver::iterator endpoint_iterator);

public:
    supervisor_connection(config::config_t config,
                           supervisor_buffer_handler buffer_handler);
    ~supervisor_connection();

    // Take the ownership of msg.
    void publish_msg(unsigned char* msg, size_t len, supervisor_buffer_deleter deleter);
};
}  // namespace vNerve::bilibili::live::worker_supervisor