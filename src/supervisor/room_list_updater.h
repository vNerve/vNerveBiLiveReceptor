#pragma once

#include <boost/asio.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/thread.hpp>

#include <vector>
#include <functional>

#include <httplib.h>

namespace vNerve::bilibili::info
{
using vtuber_info_update_callback = std::function<void(std::vector<int>&)>;

class vtuber_info_updater
{
private:
    boost::asio::io_context _context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> _guard;
    boost::thread _thread;
    std::unique_ptr<boost::asio::deadline_timer> _timer;
    int _update_interval_min;
    std::string _server_endpoint;

    httplib::Client _http_client;
    httplib::Headers _http_headers;

    std::shared_ptr<boost::program_options::variables_map> _options;

    vtuber_info_update_callback _callback;

    void reschedule_timer();
    void refresh();
    void on_timer_tick(const boost::system::error_code& ec);
public:
    vtuber_info_updater(
        std::shared_ptr<boost::program_options::variables_map> options,
        vtuber_info_update_callback callback);
    ~vtuber_info_updater();
};
}
