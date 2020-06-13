#include "global_context.h"

namespace vNerve::bilibili
{


worker_global_context::worker_global_context(config::config_t config)
    : _config(config),
      _conn_manager(config,
          std::bind(&worker_global_context::on_room_failed, this, std::placeholders::_1),
          std::bind(&worker_global_context::on_room_data, this, std::placeholders::_1, std::placeholders::_2)),
      _session(config,
          std::bind(&worker_global_context::on_request_connect_room, this, std::placeholders::_1),
               std::bind(&worker_global_context::on_request_disconnect_room, this, std::placeholders::_1)),
      _token_updater(config, std::bind(&worker_global_context::on_update_live_chat_config, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3))
{
}

worker_global_context::~worker_global_context()
{
}

void worker_global_context::join()
{
    _session.join();
}

void worker_global_context::on_room_failed(int room_id)
{
    _session.on_room_failed(room_id);
}

void worker_global_context::on_room_data(int room_id, const borrowed_message* msg)
{
    _session.on_message(room_id, msg);
}

void worker_global_context::on_request_connect_room(int room_id)
{
    _conn_manager.open_connection(room_id);
}

void worker_global_context::on_request_disconnect_room(int room_id)
{
    _conn_manager.close_connection(room_id);
}

void worker_global_context::on_update_live_chat_config(const std::string& host, int port, const std::string& token)
{
    _conn_manager.set_chat_server_config(host, port, token);
}
}
