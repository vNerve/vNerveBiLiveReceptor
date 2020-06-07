#pragma once

#include "config.h"
#include "simple_worker_proto_handler.h"
#include "asio_socket_write_helper.h"
#include "type.h"

#include <memory>
#include <random>
#include <deque>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <robin_hood.h>

namespace vNerve::bilibili::worker_supervisor
{
using supervisor_buffer_handler =
    std::function<void(identifier_t,
                       unsigned char* , size_t )>;
using supervisor_buffer_deleter = std::function<void(unsigned char*)>;
using supervisor_tick_handler = std::function<void()>;
using supervisor_new_worker_handler = std::function<void(identifier_t)>;
using supervisor_worker_disconnect_handler = std::function<void(identifier_t)>;

class worker_session
{
private:
    identifier_t _identifier;
    std::shared_ptr<boost::asio::ip::tcp::socket> _socket;
    asio_socket_write_helper _write_helper;
    simple_worker_proto_handler _read_handler;
    supervisor_worker_disconnect_handler _disconnect_handler;
    std::deque<std::tuple<unsigned char*, size_t, supervisor_buffer_deleter>> _write_queue;

    void start_async_write();
    void on_written(const boost::system::error_code& ec, size_t byte_transferred, int buffer_count);

public:
    worker_session(
        identifier_t identifier,
        std::shared_ptr<boost::asio::ip::tcp::socket> socket,
        size_t read_buffer_size,
        supervisor_buffer_handler buffer_handler, supervisor_worker_disconnect_handler disconnect_handler);
    ~worker_session();

    void send(unsigned char*, size_t, supervisor_buffer_deleter);
    void disconnect(bool callback);
    std::shared_ptr<boost::asio::ip::tcp::socket> socket() { return _socket; }

    worker_session(const worker_session& other) = delete;
    worker_session& operator=(const worker_session& other) = delete;
    worker_session(worker_session&& other) noexcept
        : _identifier(other._identifier),
          _socket(std::move(other._socket)),
          _read_handler(std::move(other._read_handler)),
          _write_helper(std::move(other._write_helper)),
          _disconnect_handler(std::move(other._disconnect_handler)),
          _write_queue(std::move(other._write_queue))
    {
    }

    worker_session& operator=(worker_session&& other) noexcept
    {
        if (this == &other)
            return *this;
        _identifier = other._identifier;
        _socket = std::move(other._socket);
        _read_handler = std::move(other._read_handler);
        _write_helper = std::move(other._write_helper);
        _disconnect_handler = std::move(other._disconnect_handler);
        _write_queue = std::move(other._write_queue);
        return *this;
    }
};

class worker_connection_manager
    : public std::enable_shared_from_this<worker_connection_manager>
{
private:
    config::config_t _config;

    boost::asio::io_context _context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        _guard;
    robin_hood::unordered_map<identifier_t, worker_session> _sockets;
    boost::asio::ip::tcp::acceptor _acceptor;
    std::unique_ptr<boost::asio::deadline_timer> _timer;
    int _timer_interval_ms;
    size_t _read_buffer_size;

    boost::thread _thread;
    supervisor_buffer_handler _buffer_handler;
    supervisor_tick_handler _tick_handler;
    supervisor_new_worker_handler _new_worker_handler;
    supervisor_worker_disconnect_handler _disconnect_handler;

    void start_accept();
    void on_accept(const boost::system::error_code&,
                   std::shared_ptr<boost::asio::ip::tcp::socket>);

    std::random_device _rand;
    std::mt19937_64 _rand_engine;
    std::uniform_int_distribution<identifier_t> _rand_dist;

    void reschedule_timer();
    void on_timer_tick(const boost::system::error_code& ec);

public:
    worker_connection_manager(config::config_t config,
                              supervisor_buffer_handler,
                              supervisor_tick_handler,
                              supervisor_new_worker_handler,
                              supervisor_worker_disconnect_handler);
    ~worker_connection_manager();

    worker_connection_manager(worker_connection_manager& another) = delete;
    worker_connection_manager(worker_connection_manager&& another) = delete;
    worker_connection_manager& operator =(worker_connection_manager & another) = delete;
    worker_connection_manager& operator =(worker_connection_manager && another) = delete;

    /// @param msg Message to be sent. Taking ownership of msg
    void send_message(identifier_t identifier, unsigned char* msg, size_t len, supervisor_buffer_deleter deleter);
    void disconnect_worker(identifier_t identifier, bool callback = false);

    boost::asio::io_context& context() { return _context; }
};
}  // namespace vNerve::bilibili::worker_supervisor