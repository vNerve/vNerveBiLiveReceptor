#pragma once
#include "bilibili_connection_manager.h"
#include "bilibili_token_updater.h"
#include "supervisor_session.h"
#include "config.h"

namespace vNerve::bilibili
{
class worker_global_context
{
private:
    config::config_t _config;

    bilibili_connection_manager _conn_manager;
    worker_supervisor::supervisor_session _session;

    bilibili_token_updater _token_updater;

public:
    worker_global_context(config::config_t);
    ~worker_global_context();
};
}
