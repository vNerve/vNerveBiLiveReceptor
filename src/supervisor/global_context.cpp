#include "global_context.h"

#include <chrono>
#include "command_handler_sv.h"

namespace vNerve::bilibili
{

supervisor_global_context::supervisor_global_context(const config::config_sv_t config, const config::config_linker_t config_linker)
    : _amqp_context(config),
      _deduplicate_context(&config->message.message_ttl_sec),
      _scheduler(
          std::make_shared<worker_supervisor::scheduler_session>(
              config, config_linker,
              std::bind(&supervisor_global_context::on_worker_data, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4),
              std::bind(&supervisor_global_context::on_diagnostic_data, this, std::placeholders::_1, std::placeholders::_2),
              std::bind(&supervisor_global_context::on_server_tick, this))),
      _config_linker(config_linker),
      _room_list_updater(std::make_shared<info::vtuber_info_updater>(config, std::bind(&supervisor_global_context::on_vtuber_list_update, this, std::placeholders::_1)))
{
    _room_list_updater->init();
}

supervisor_global_context::~supervisor_global_context()
{
}

void supervisor_global_context::join()
{
    _scheduler->join();
}

void supervisor_global_context::handle_command(std::string_view input)
{
    command::handle_command_sv(input, _config_linker.get());
}

void supervisor_global_context::on_vtuber_list_update(std::vector<int>& room_ids)
{
    _scheduler->update_room_lists(room_ids);
}

void supervisor_global_context::on_worker_data(checksum_t checksum, std::string_view routing_key, unsigned char const* data, size_t len)
{
    if (checksum == 0 || _deduplicate_context.check_and_add(checksum)) // see simple_worker_proto.h
        _amqp_context.post_payload(routing_key, data, len);
}

void supervisor_global_context::on_diagnostic_data(unsigned char const* data, size_t len)
{
    _amqp_context.post_diag_payload(data, len);
}

void supervisor_global_context::on_server_tick()
{
    _deduplicate_context.check_expire();
}
}
