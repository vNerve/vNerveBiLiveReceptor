#pragma once

#include <boost/asio.hpp>
#include <boost/noncopyable.hpp>
#include <boost/thread/thread_only.hpp>
#include <boost/program_options.hpp>

#include <amqpcpp.h>
#include "config.h"

namespace vNerve::bilibili::mq
{
const size_t write_buf_default_size = 32 * 1024; // 32 K

class amqp_asio_connection : public AMQP::ConnectionHandler, boost::noncopyable
{
private:
    static const int READ_BUFFER_LEN = 4096 * 2;
    static const int MIN_HEARTBEAT_LEN_SEC = 60;
    AMQP::Connection* _connection = nullptr;
    bool _initializing = false;
    AMQP::Login _login;
    std::string _vhost;
    int _heartbeat_interval_sec = 60;
    int _reconnect_interval_sec = 60;

    boost::asio::io_context _context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _guard;
    boost::asio::ip::tcp::resolver _resolver;
    boost::thread _thread;
    boost::asio::deadline_timer _timer;
    boost::asio::deadline_timer _reconnect_timer;

    std::shared_ptr<boost::asio::ip::tcp::socket> _socket;
    std::unique_ptr<unsigned char[]> _read_buffer_guard;
    unsigned char* _read_buffer;
    size_t _buffer_last_remaining = 0;

    std::function<void()> _onReady;

    std::string _host;
    int _port;

    void onData(AMQP::Connection* connection, const char* buffer, size_t size) override;
    void onError(AMQP::Connection* connection, const char* message) override;
    void onReady(AMQP::Connection* connection) override;
    void onClosed(AMQP::Connection* connection) override;
    uint16_t onNegotiate(AMQP::Connection* connection, uint16_t interval) override;

    void close_socket(bool active = false);
    void start_async_read();
    void start_heartbeat_timer();
    void start_reconnect_timer();

    void on_timer_tick(const boost::system::error_code& ec);
    void on_reconnection_timer_tick(const boost::system::error_code& ec);
    void on_received(const boost::system::error_code& ec, size_t transferred);

public:
    amqp_asio_connection(const std::string& host, int port, const AMQP::Login& login, const std::string& vhost, int reconnect_interval_sec);
    AMQP::Connection* connection() const { return _connection; }
    operator AMQP::Connection*() const { return _connection; }

    // Must be running on AMQP thread.
    bool reconnect();
    bool reconnect(std::function<void()> onReady);
    void post(std::function<void()> func);
    void disconnect();
};

class amqp_context
{
private:
    amqp_asio_connection _connection;
    AMQP::Channel* _channel = nullptr;

    std::string _exchange;
    std::string _diag_exchange;
    bool _available = false;

    unsigned char* _write_buf;
    size_t _write_buf_len;

    void on_ready();

public:
    amqp_context(config::config_t options);
    ~amqp_context();

    void post_payload(std::string_view routing_key, unsigned char const* payload, size_t len);
    void post_diag_payload(unsigned char const* payload, size_t len);
};
}
