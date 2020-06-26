#pragma once
#include "config.h"

#include <boost/asio.hpp>

namespace vNerve::bilibili
{
struct bilibili_live_config
{
    std::string host;
    int port;
    std::string token;
};

void async_fetch_bilibili_live_config(
    boost::asio::io_context& context,
    config::config_t config,
    int room_id,
    std::function<void(bilibili_live_config const&)> on_success,
    std::function<void()> on_failed);
}
