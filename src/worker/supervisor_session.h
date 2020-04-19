#pragma once

#include "supervisor_connection.h"
#include "config.h"

#include <memory>

namespace vNerve::bilibili::worker_supervisor
{
class supervisor_session : std::enable_shared_from_this<supervisor_session>
{
    config::config_t _config;
    supervisor_connection _connection;

    void on_supervisor_connected();
    void on_supervisor_message(unsigned char*, size_t);

public:
    void on_data(unsigned char* data, size_t len);

    supervisor_session(config::config_t config);
    ~supervisor_session();
};
}