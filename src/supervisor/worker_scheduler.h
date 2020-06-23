#pragma once
#include "config.h"
#include "type.h"
#include "worker_connection_manager.h"
#include "worker_scheduler_types.h"
#include "diagnostic_context.h"

#include <memory>

#include <chrono>

namespace vNerve::bilibili
{

namespace worker_supervisor
{
class scheduler_session
{
    // REFER TO worker_scheduler_types.h
private:
    std::shared_ptr<worker_connection_manager> _worker_session;

    tasks_set _tasks;
    rooms_map _rooms;
    workers_map _workers;

    config::config_t _config;
    supervisor_diagnostics_context _diag_context;

    std::chrono::system_clock::time_point _last_checked;
    std::chrono::system_clock::duration _min_check_interval;
    std::chrono::system_clock::duration _worker_interval_threshold;
    std::chrono::system_clock::duration _worker_penalty;
    int _max_new_tasks_per_bunch;

    supervisor_data_handler _data_handler;
    supervisor_diag_data_handler _diag_data_handler;
    supervisor_server_tick_handler _tick_handler;

    char _auth_code[auth_code_size + 1];

    ///
    /// 清空属于该 worker 的所有任务。\n
    /// Warning: not notifying the worker! \n
    /// <b>不会更新 Worker 和 Room 的计数器！</b>
    void clear_worker_tasks(identifier_t identifier);
    ///
    /// 重置 worker 状态，清空 worker 任务并置于未初始化状态。
    void reset_worker(worker_status* worker);
    ///
    /// 重置（清空任务），并删除 worker。
    void delete_worker(worker_status* worker);
    ///
    /// 重置、删除 worker，并断开到 worker 的连接。
    void delete_and_disconnect_worker(worker_status* worker);

    ///
    /// Delete a task and update connection count on worker and room.
    /// @param iter Iterator to the task in _tasks.
    /// @param desc_rank Whether to decrease the rank of the worker or not. When you aren't disconnecting the task because of error, this should be false.
    template <int N, class Iterator>
    Iterator delete_task(Iterator iter, bool desc_rank = true);
    ///
    /// Delete a task and update connection count on worker and room.
    /// @param identifier Identifier of the worker.
    /// @param room_id Room id of the room.
    /// @param desc_rank Whether to decrease the rank of the worker or not. When you aren't disconnecting the task because of error, this should be false.
    void delete_task(identifier_t identifier, room_id_t room_id, bool desc_rank = true);

    ///
    /// Assign a task to a worker. 将会更新 room 与 worker 的计数器. \n
    /// Assignment packet will be sent to worker.
    void assign_task(worker_status* worker, room_status* room, std::chrono::system_clock::time_point now);

    ///
    /// Calculate the maximum count of workers connecting to one single room.
    static int calculate_max_workers_per_room(std::vector<worker_status*>& workers_available, int room_count);
    ///
    /// 强制更新 worker 和 room 的连接计数器
    void refresh_counts();
    ///
    /// Check last received interval for workers and tasks. \n
    /// Workers and tasks exceeding maximum interval will be disconnected. \n
    /// Tasks will be unassigned(with notify to the worker).
    void check_worker_task_interval();
    ///
    /// Check all states, schedule all tasks.
    /// <b>This function is costly, so a internal frequency limiter is included.</b>
    /// This should be called periodically.
    void check_all_states();
    void update_diagnostics(int max_tasks_per_room);

    void send_assign(identifier_t identifier, room_id_t room);
    void send_unassign(identifier_t identifier, room_id_t room);
    ///
    /// Called when a new worker connected but didn't sent WORKER_READY packet yet.
    void handle_new_worker(identifier_t identifier);
    void handle_buffer(identifier_t identifier, unsigned char* payload_data, size_t payload_len);
    void handle_worker_disconnect(identifier_t identifier);
    void send_to_identifier(identifier_t identifier, unsigned char* payload,
                            size_t size, std::function<void(unsigned char*)> deleter);

public:
    scheduler_session(config::config_t config, supervisor_data_handler data_handler, supervisor_diag_data_handler diag_data_handler, supervisor_server_tick_handler tick_handler);
    ~scheduler_session();

    void update_room_lists(std::vector<int>&);
    void join();
};
}  // namespace worker_supervisor
}
