#pragma once
#include "config.h"
#include "type.h"
#include "supervisor_server_session.h"

#include <boost/asio/detail/socket_ops.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>

#include <memory>

#include <robin_hood.h>
#include <vector.hpp>

#include <chrono>

namespace vNerve::bilibili
{

namespace worker_supervisor
{
using simple_buffer = std::pair<std::unique_ptr<unsigned char[]>, size_t>;
template <typename Key, typename Value>
using unordered_map = robin_hood::unordered_map<Key, Value>;
template <typename Element>
using vector = lni::vector<Element>;
template <typename Element>
using unordered_set = robin_hood::unordered_set<Element>;

struct worker_status;
struct room_status;

struct room_task
{
    identifier_t identifier;
    room_id_t room_id;

    std::chrono::system_clock::time_point last_received;
    //std::weak_ptr<worker_status> worker; // is use shared_ptr + weak_ptr better than looking up unordered_map?
    //std::weak_ptr<room_status> room;

    room_task(identifier_t identifier, room_id_t room_id)
        : identifier(identifier), room_id(room_id) {}
};

struct worker_status
{
    identifier_t identifier;
    std::chrono::system_clock::time_point last_received;

    bool initialized = false;
    int max_rooms = -1;
    ///
    /// Not necessarily real-time!
    int current_connections = 0;

    double rank = 0.0;

    worker_status(identifier_t identifier, std::chrono::system_clock::time_point first_received)
        : identifier(identifier), last_received(first_received)
    {

    }
};

struct room_status
{
    room_id_t room_id;

    bool active = true;
    ///
    /// Not necessarily real-time!
    int current_connections = 0;

    room_status(int room_id)
        : room_id(room_id) {}
};

using tasks_set =
    boost::multi_index_container<
        room_task,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<boost::multi_index::composite_key<
                room_task,
                boost::multi_index::member<room_task, identifier_t, &room_task::identifier>,
                boost::multi_index::member<room_task, room_id_t, &room_task::room_id>
            >>,
            boost::multi_index::hashed_non_unique<boost::multi_index::member<room_task, identifier_t, &room_task::identifier>>,
            boost::multi_index::hashed_non_unique<boost::multi_index::member<room_task, room_id_t, &room_task::room_id>>
        >
    >;
inline const int tasks_by_identifier_and_room_id = 0;
inline const int tasks_by_identifier = 1;
inline const int tasks_by_room_id = 2;
using tasks_by_identifier_and_room_id_t = tasks_set::nth_index<tasks_by_identifier_and_room_id>::type;
using tasks_by_identifier_t = tasks_set::nth_index<tasks_by_identifier>::type;
using tasks_by_room_id_t = tasks_set::nth_index<tasks_by_room_id>::type;

using rooms_map = unordered_map<room_id_t, room_status>;
using workers_map = unordered_map<identifier_t, worker_status>;

class scheduler_session : std::enable_shared_from_this<scheduler_session>
{
private:
    std::shared_ptr<supervisor_server_session> _worker_session;

    tasks_set _tasks;
    rooms_map _rooms;
    workers_map _workers;

    config::config_t _config;

    std::chrono::system_clock::time_point _last_checked;
    std::chrono::system_clock::duration _min_check_interval;
    std::chrono::system_clock::duration _worker_interval_threshold;

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
    void assign_task(worker_status* worker, room_status* room);

    ///
    /// Calculate the maximum count of workers connecting to one single room.
    int calculate_max_workers_per_room(std::vector<worker_status*>& workers_available, int rooms);
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

    void send_assign(identifier_t identifier, room_id_t room);
    void send_unassign(identifier_t identifier, room_id_t room);
    ///
    /// Called when a new worker connected but didn't sent WORKER_READY packet yet.
    void handle_new_worker(identifier_t identifier);
    void handle_buffer(identifier_t identifier, unsigned char* payload_data, size_t payload_len);
    void send_to_identifier(identifier_t identifier, unsigned char* payload,
                            size_t size, std::function<void(unsigned char*)> deleter);

public:
    scheduler_session(config::config_t config);
    ~scheduler_session();
};
}  // namespace worker_supervisor
}
