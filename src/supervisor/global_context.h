#pragma once

#include "config.h"
#include "amqp_client.h"
#include "deduplicate_context.h"
#include "room_list_updater.h"
#include "worker_scheduler.h"

#include <memory>

namespace vNerve::bilibili
{
class supervisor_global_context
{
private:
    mq::amqp_context _amqp_context;
    deduplicate_context _deduplicate_context;
    std::shared_ptr<worker_supervisor::scheduler_session> _scheduler;
    config::config_linker_t _config_linker;

    std::shared_ptr<info::vtuber_info_updater> _room_list_updater;

    void on_vtuber_list_update(std::vector<int>&);
    void on_worker_data(checksum_t, std::string_view, unsigned char const*, size_t);
    void on_diagnostic_data(unsigned char const*, size_t);
    void on_server_tick();

public:
    supervisor_global_context(config::config_sv_t, config::config_linker_t);
    ~supervisor_global_context();

    void join();
    void handle_command(std::string_view input);
};
}