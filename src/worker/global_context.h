#pragma once
#include "bilibili_connection_manager.h"
#include "bilibili_token_updater.h"
#include "supervisor_session.h"
#include "borrowed_message.h"
#include "config.h"

#include <memory>

namespace vNerve::bilibili
{
class worker_global_context
{
private:
    config::config_t _config;

    bilibili_connection_manager _conn_manager;
    worker_supervisor::supervisor_session _session;

    //std::shared_ptr<bilibili_token_updater> _token_updater;

    void on_room_failed(int room_id);
    void on_room_data(int room_id, const borrowed_message*);
    void on_request_connect_room(int room_id);
    void on_request_disconnect_room(int room_id);
    void on_supervisor_disconnected();

public:
    worker_global_context(config::config_t);
    ~worker_global_context();

    void join();
};
}
