#include "diagnostic_context.h"

namespace vNerve::bilibili::worker_supervisor
{
supervisor_diagnostics_context::supervisor_diagnostics_context()
    : _buf(new unsigned char[diagnostics_max_size]),
      _diag_message(google::protobuf::Arena::CreateMessage<live::BilibiliLiveSupervisorDiagnostics>(&_arena))
{

}

void supervisor_diagnostics_context::update_diagnostics(rooms_map const& rooms, workers_map const& workers, tasks_set const& tasks, int max_tasks_per_room)
{
    _diag_message->Clear();
    _current_size = 0;

    for (auto const& [room_id, room] : rooms)
    {
        auto room_status_msg = _diag_message->add_room_statuses();
        room_status_msg->set_id(room_id);
        room_status_msg->set_current_connections(room.current_connections);
    }

    for (auto const& [worker_identifier, worker] : workers)
    {
        auto worker_status_msg = _diag_message->add_worker_statuses();
        worker_status_msg->set_id(worker_identifier);
        worker_status_msg->set_current_connections(worker.current_connections);
        worker_status_msg->set_allow_new_task_after(std::chrono::duration_cast<std::chrono::seconds>(worker.allow_new_task_after.time_since_epoch()).count());
        worker_status_msg->set_max_rooms(worker.max_rooms);
    }

    for (auto const& task : tasks)
    {
        auto task_msg = _diag_message->add_tasks();
        task_msg->set_room_id(task.room_id);
        task_msg->set_worker_id(task.identifier);
    }

    _diag_message->set_max_tasks_per_room(max_tasks_per_room);

    _current_size = _diag_message->ByteSizeLong();
    _diag_message->SerializeToArray(_buf.get(), diagnostics_max_size);
}
}
