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
    ///
    /// Called when received a message from the supervisor
    /// The data which the function is called with DOESN'T contains header.
    /// i.e. the pointer points to the PAYLOAD of the data.
    void on_supervisor_message(unsigned char*, size_t);

public:
    ///
    /// Send data to the supervisor.
    /// The data being sent must have been prepended with the size of the payload.
    /// i.e. The data must be enveloped into a "simple worker protocol".
    ///
    /// This functions takes ownership of the data and deletes it after usage using the given deleter.
    void on_data(unsigned char* data, size_t len, supervisor_buffer_deleter deleter);

    supervisor_session(config::config_t config);
    ~supervisor_session();
};
}