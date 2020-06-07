#include "global_context.h"

#include <chrono>
namespace vNerve::bilibili
{

supervisor_global_context::supervisor_global_context(const config::config_t config)
    : _amqp_context(config),
      _deduplicate_context(std::chrono::seconds((*config)["message-ttl-sec"].as<int>())),
      _scheduler(
          std::make_shared<worker_supervisor::scheduler_session>(
              config,
              std::bind(&supervisor_global_context::on_worker_data, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
              std::bind(&supervisor_global_context::on_server_tick, this))),
      _room_list_updater(config, std::bind(&supervisor_global_context::on_vtuber_list_update, this, std::placeholders::_1))
{
}

supervisor_global_context::~supervisor_global_context()
{
}

void supervisor_global_context::on_vtuber_list_update(std::vector<int>& room_ids)
{
    _scheduler->update_room_lists(room_ids);
}

void supervisor_global_context::on_worker_data(checksum_t checksum, std::string_view routing_key, unsigned char const* data, size_t len)
{
    if (_deduplicate_context.check_and_add(checksum))
        _amqp_context.post_payload(routing_key, data, len);
}

void supervisor_global_context::on_server_tick()
{
    _deduplicate_context.check_expire();
}
}
