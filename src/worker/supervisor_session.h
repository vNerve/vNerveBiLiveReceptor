#pragma once

#include "supervisor_connection.h"
#include "config.h"

#include <memory>

namespace vNerve::bilibili::worker_supervisor
{
typedef void(zmq_memory_deleter)(void** buf);

class supervisor_session : std::enable_shared_from_this<supervisor_session>
{
    config::config_t _config;
    supervisor_connection _connection;

    void send_worker_ready();
    void on_supervisor_message(unsigned char*, size_t);

public:
    void on_data(unsigned char* data, size_t len, zmq_memory_deleter deleter);

    supervisor_session(config::config_t config);
    ~supervisor_session();
};
}