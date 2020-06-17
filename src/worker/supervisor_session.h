#pragma once

#include "supervisor_connection.h"
#include "config.h"
#include "borrowed_message.h"
#include "type.h"

#include <memory>

namespace vNerve::bilibili::worker_supervisor
{
using room_operation_handler = std::function<void(int)>;
using supervisor_operation_handler = std::function<void()>;

class supervisor_session
{
    config::config_t _config;
    supervisor_connection _connection;

    int _max_rooms;

    room_operation_handler _on_open_connection;
    room_operation_handler _on_close_connection;
    supervisor_operation_handler _on_supervisor_disconnected;

    void on_supervisor_connected();
    void on_supervisor_disconnected();
    ///
    /// Called when received a message from the supervisor
    /// The data which the function is called with DOESN'T contains header.
    /// i.e. the pointer points to the PAYLOAD of the data.
    void on_supervisor_message(unsigned char*, size_t);

    ///
    /// Send data to the supervisor.
    /// The data being sent must have been prepended with the size of the payload.
    /// i.e. The data must be enveloped into a "simple worker protocol".
    ///
    /// This functions takes ownership of the data and deletes it after usage using the given deleter.
    void on_data(unsigned char* data, size_t len, supervisor_buffer_deleter deleter);

public:

    void on_message(room_id_t room_id, borrowed_message const* msg);
    void on_room_failed(room_id_t room_id);
    void join();

    supervisor_session(config::config_t config, room_operation_handler on_open_connection, room_operation_handler on_close_connection, supervisor_operation_handler on_supervisor_disconnected);
    ~supervisor_session();
};
}
