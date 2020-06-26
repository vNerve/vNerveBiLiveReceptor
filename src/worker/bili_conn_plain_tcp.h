#pragma once

#include <memory>

#include <boost/asio.hpp>

namespace vNerve::bilibili
{
class bilibili_connection_manager;
class bilibili_connection_plain_tcp : public std::enable_shared_from_this<bilibili_connection_plain_tcp>
{
private:
    std::unique_ptr<unsigned char[]> _read_buffer_ptr;
    size_t _read_buffer_size;
    size_t _read_buffer_offset = 0;
    size_t _skipping_bytes = 0;

    bilibili_connection_manager* _session;
    std::shared_ptr<boost::asio::ip::tcp::socket> _socket;

    std::unique_ptr<boost::asio::deadline_timer> _heartbeat_timer;

    int _room_id;
    std::string_view _token;
    int _heartbeat_interval_sec;

    bool _closed = false;

    void reschedule_timer();
    void start_read();

    void on_connected(const boost::system::error_code& err);
    void on_join_room_sent(const boost::system::error_code&, size_t,
                           std::string*);
    void on_heartbeat_sent(const boost::system::error_code&, size_t);
    void on_heartbeat_tick(const boost::system::error_code&);
    void on_receive(const boost::system::error_code&, size_t);

public:
    bilibili_connection_plain_tcp(bilibili_connection_manager* session, int room_id, std::string_view token);
    ~bilibili_connection_plain_tcp();

    bilibili_connection_plain_tcp(const bilibili_connection_plain_tcp& other) = delete;
    bilibili_connection_plain_tcp& operator=(const bilibili_connection_plain_tcp& other) = delete;

    void init(const boost::asio::ip::tcp::resolver::iterator& endpoints);
    void close(bool failed = false);
    bool closed() const { return _closed; }
};
}  // namespace vNerve::bilibili
