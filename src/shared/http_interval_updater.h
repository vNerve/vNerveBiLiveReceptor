#pragma once

#include <curl/curl.h>

#include <boost/asio.hpp>
#include <boost/thread/thread_only.hpp>
#include <boost/chrono.hpp>

namespace vNerve::bilibili
{
class http_interval_updater : public std::enable_shared_from_this<http_interval_updater>
{
private:
    boost::asio::io_context _context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _guard;
    // MUST BE SINGLE-THREADED
    boost::thread _thread;
    std::unique_ptr<boost::asio::deadline_timer> _timer;

    CURL* _curl;

    void reschedule_timer();
    void refresh();
    void on_timer_tick(const boost::system::error_code& ec);
    void setup_curl();

protected:
    virtual const char* on_request_url() = 0;
    virtual const char* on_request_method() = 0;
    virtual const char* on_request_payload() = 0;
    virtual const char* on_request_content_type() = 0;
    virtual const char* on_user_agent() = 0;
    virtual const char* on_request_accept() = 0;
    virtual const char* on_request_referer() = 0;
    virtual int on_update_interval_sec() = 0;
    virtual int on_timeout_sec() = 0;

    virtual void on_updated(std::string_view) = 0;

public:
    http_interval_updater();
    virtual ~http_interval_updater();

    void init();
};
}