#pragma once

#include "worker_scheduler_types.h"
#include "vNerve/bilibili/live/diagnostics.pb.h"

#include <memory>

#include <google/protobuf/arena.h>

namespace vNerve::bilibili::worker_supervisor
{
const size_t diagnostics_max_size = 512 * 1024;

class supervisor_diagnostics_context
{
private:
    google::protobuf::Arena _arena;
    std::unique_ptr<unsigned char[]> _buf;
    size_t _current_size = 0;

    live::BilibiliLiveSupervisorDiagnostics* _diag_message;

public:
    supervisor_diagnostics_context();

    void update_diagnostics(rooms_map const& rooms, workers_map const& workers, tasks_set const& tasks, int max_tasks_per_room);
    unsigned char* data() const { return _buf.get(); };
    size_t size() const { return _current_size; };
};
}
