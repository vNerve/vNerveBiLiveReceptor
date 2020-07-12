#pragma once

#include "bilibili_live_config.h"
#include <memory>

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace vNerve::bilibili
{
class bilibili_connection_manager;
class bilibili_connection_websocket : public std::enable_shared_from_this<bilibili_connection_websocket>
{
private:
    boost::asio::ip::tcp::resolver _resolver;
    boost::beast::flat_buffer _read_buffer;

    bilibili_connection_manager* _session;
    boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>> _ws_stream;
    //std::shared_ptr<boost::asio::ip::tcp::socket> _socket;

    std::unique_ptr<boost::asio::deadline_timer> _heartbeat_timer;

    int _room_id;
    std::string _token;
    std::string const* _user_agent = nullptr;
    int _heartbeat_interval_sec;

    bool _closed = false;

    void reschedule_timer();
    void start_read();

    void on_config_fetched(const bilibili_live_config& config);
    void on_resolved(const boost::system::error_code& err, boost::asio::ip::tcp::resolver::results_type endpoints);
    void on_connected(const boost::system::error_code& err);
    void on_ssl_handshake(const boost::system::error_code& err);
    void on_handshake(const boost::system::error_code& err);
    void on_join_room_sent(const boost::system::error_code&, size_t,
                           std::string*);
    void on_heartbeat_sent(const boost::system::error_code&, size_t);
    void on_heartbeat_tick(const boost::system::error_code&);
    void on_receive(const boost::system::error_code&, size_t);

public:
    bilibili_connection_websocket(bilibili_connection_manager* session, int room_id);
    ~bilibili_connection_websocket();

    bilibili_connection_websocket(const bilibili_connection_websocket& other) = delete;
    bilibili_connection_websocket& operator=(const bilibili_connection_websocket& other) = delete;

    void init();
    void close(bool failed = false);
    bool closed() const { return _closed; }
};
}  // namespace vNerve::bilibili
