#pragma once

#include "http_interval_updater.h"
#include "config_sv.h"

#include <vector>
#include <functional>

namespace vNerve::bilibili::info
{
using vtuber_info_update_callback = std::function<void(std::vector<int>&)>;
class vtuber_info_updater final : public http_interval_updater
{
private:
    std::string* _server_url;
    std::string _user_agent;

    config::config_sv_t _options;

    vtuber_info_update_callback _callback;

protected:
    const char* on_request_url() override { return _server_url->c_str(); }
    const char* on_request_method() override;
    const char* on_request_payload() override;
    const char* on_request_content_type() override;
    const char* on_user_agent() override { return _user_agent.c_str(); }
    const char* on_request_accept() override;
    const char* on_request_referer() override;
    int on_update_interval_sec() override { return _options->room_list_updater.interval_min * 60; }
    int on_timeout_sec() override { return _options->room_list_updater.timeout_sec; }
    void on_updated(std::string_view) override;

public:
    vtuber_info_updater(
        config::config_sv_t options,
        vtuber_info_update_callback callback);
    ~vtuber_info_updater() override = default;
};
}
