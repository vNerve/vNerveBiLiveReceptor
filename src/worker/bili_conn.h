#pragma once

#include <memory>

#include <boost/asio.hpp>

namespace vNerve::bilibili
{
class bilibili_connection_manager;
class bilibili_connection
{
private:
    std::unique_ptr<unsigned char[]> _read_buffer_ptr;
    size_t _read_buffer_size;
    size_t _read_buffer_offset = 0;
    size_t _skipping_bytes = 0;

    std::shared_ptr<bilibili_connection_manager> _session;
    std::shared_ptr<boost::asio::ip::tcp::socket> _socket;

    std::unique_ptr<boost::asio::deadline_timer> _heartbeat_timer;

    int _room_id;
    int _heartbeat_interval_sec;

    void reschedule_timer();
    void start_read();

    void on_join_room_sent(const boost::system::error_code&, size_t,
                           std::string*);
    void on_heartbeat_sent(const boost::system::error_code&, size_t) const;
    void on_heartbeat_tick(const boost::system::error_code&);
    void on_receive(const boost::system::error_code&, size_t);

public:
    bilibili_connection(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                        std::shared_ptr<bilibili_connection_manager> session, int room_id);
    ~bilibili_connection();

    bilibili_connection(const bilibili_connection& other) = delete;
    bilibili_connection& operator=(const bilibili_connection& other) = delete;

    bilibili_connection(bilibili_connection&& other) noexcept
    {
        _read_buffer_ptr = std::move(other._read_buffer_ptr);
        _read_buffer_size = other._read_buffer_size;
        _read_buffer_offset = other._read_buffer_offset;
        _skipping_bytes = other._skipping_bytes;
        _session = std::move(other._session);
        _socket = std::move(other._socket);
        _heartbeat_timer = std::move(other._heartbeat_timer);
        _room_id = other._room_id;
        _heartbeat_interval_sec = other._heartbeat_interval_sec;
    }
    bilibili_connection& operator=(bilibili_connection&& other) noexcept
    {
        if (this == &other)
            return *this;
        _read_buffer_ptr = std::move(other._read_buffer_ptr);
        _read_buffer_size = other._read_buffer_size;
        _read_buffer_offset = other._read_buffer_offset;
        _skipping_bytes = other._skipping_bytes;
        _session = std::move(other._session);
        _socket = std::move(other._socket);
        _heartbeat_timer = std::move(other._heartbeat_timer);
        _room_id = other._room_id;
        _heartbeat_interval_sec = other._heartbeat_interval_sec;
        return *this;
    }
};
}  // namespace vNerve::bilibili
