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

    boost::posix_time::time_duration _update_interval;

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

    virtual void on_updated(std::string_view) = 0;

public:
    http_interval_updater(int update_interval_min, int timeout_sec)
        : http_interval_updater(boost::posix_time::minutes(update_interval_min), timeout_sec) {}
    http_interval_updater(boost::posix_time::time_duration update_interval, int timeout_sec);
    virtual ~http_interval_updater();

    void init();
};
}