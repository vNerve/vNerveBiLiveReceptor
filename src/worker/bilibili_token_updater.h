#pragma once

#include "http_interval_updater.h"
#include "config.h"

#include <functional>

namespace vNerve::bilibili
{
using bilibili_token_updater_callback = std::function<void(const std::string&, int, const std::string&)>;

class bilibili_token_updater : public http_interval_updater
{
private:
    std::string _url{};
    std::string _user_agent{};
    std::string _referer{};
    bilibili_token_updater_callback _callback{};

    const char* on_request_url() override { return _url.c_str(); }
    const char* on_request_method() override;
    const char* on_request_payload() override { return nullptr; }
    const char* on_request_content_type() override { return nullptr; }
    const char* on_user_agent() override { return _user_agent.c_str(); }
    const char* on_request_accept() override;
    const char* on_request_referer() override { return _referer.c_str(); }
    void on_updated(std::string_view) override;
public:
    bilibili_token_updater(config::config_t config, bilibili_token_updater_callback callback);
    ~bilibili_token_updater() override = default;
};

}