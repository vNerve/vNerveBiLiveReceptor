#pragma once

#include <string>
#include <memory>
#include "config.h"

namespace vNerve::bilibili::config
{
struct config_supervisor
{
    struct config_amqp
    {
        std::string host;
        int port;
        std::string user;
        std::string password;
        std::string vhost;
        int reconnect_interval_sec;

        std::string exchange;
        std::string diag_exchange;
    } amqp;

    struct config_room_list_updater
    {
        std::string url;
        int interval_min;
        int timeout_sec;
    } room_list_updater;

    struct config_worker
    {
        int port;
        std::string auth_code;

        int worker_timeout_sec;
        int check_interval_msec;
        int min_check_interval_msec;
        int worker_penalty_min;

        size_t read_buffer_size;

        int max_new_tasks_per_bunch;
    } worker;

    struct config_message
    {
        int message_ttl_sec;
        int min_interval_popularity_sec;
    } message;
};

using config_sv_t = std::shared_ptr<config_supervisor>;

// See config.cpp
config_sv_t fill_config(config_t raw);
std::shared_ptr<config_dynamic_linker> link_config(config_sv_t config);
}
