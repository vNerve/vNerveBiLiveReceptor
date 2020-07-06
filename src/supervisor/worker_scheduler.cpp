#include "worker_scheduler.h"

#include "worker_connection_manager.h"
#include "simple_worker_proto.h"
#include "simple_worker_proto_generator.h"
#include "profiler.h"

#include <algorithm>
#include <boost/range/adaptors.hpp>
#include <spdlog/spdlog.h>
#include <vector.hpp>

#define LOG_PREFIX "[w_sched] "

namespace vNerve::bilibili::worker_supervisor
{
supervisor_buffer_deleter unsigned_char_array_deleter = [](unsigned char* buf) -> void
{
    delete[] buf;
};

bool compare_worker(const worker_status* lhs, const worker_status* rhs)
{
    return lhs->max_rooms - lhs->current_connections > rhs->max_rooms - rhs->current_connections;
}

// =============================== scheduler_session ===============================

scheduler_session::scheduler_session(
    const config::config_sv_t config, const config::config_linker_t config_linker,
    supervisor_data_handler data_handler, supervisor_diag_data_handler diag_data_handler, supervisor_server_tick_handler tick_handler)
    : _config(config),
      _config_linker(config_linker),
      _data_handler(std::move(data_handler)),
      _diag_data_handler(std::move(diag_data_handler)),
      _tick_handler(std::move(tick_handler))
{
    _worker_session = std::make_shared<worker_connection_manager>(
        config,
        std::bind(&scheduler_session::handle_buffer, this,
                  std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3),
        std::bind(&scheduler_session::check_all_states, this),
        std::bind(&scheduler_session::handle_new_worker, this,
                  std::placeholders::_1),
        std::bind(&scheduler_session::handle_worker_disconnect, this, std::placeholders::_1));
    load_auth_code();

    _config_linker->register_listener(this, std::bind(&scheduler_session::on_config_updated, this, std::placeholders::_1));
}

scheduler_session::~scheduler_session()
{
    _config_linker->unregister_listener(this);
}

void scheduler_session::update_room_lists(std::vector<int>& rooms)
{
    auto rooms_set = std::set(rooms.begin(), rooms.end());
    post(_worker_session->context().get_executor(), [this, rooms_set]() {
        int counter1 = 0, counter2 = 0;
        for (auto& [room_id, room] : _rooms)
            if (rooms_set.find(room_id) == rooms_set.end())
            {
                room.active = false;
                counter1++;
            }
        for (auto room_id : rooms_set)
            if (_rooms.find(room_id) == _rooms.end())
            {
                _rooms.emplace(
                    std::piecewise_construct,
                    std::forward_as_tuple(room_id),
                    std::forward_as_tuple(room_id));
                counter2++;
            }
        if (counter1 + counter2 > 0)
            spdlog::info(LOG_PREFIX "Updating room list: deleting {}, adding {}", counter1, counter2);
    });
}

void scheduler_session::join()
{
    _worker_session->join();
}

void scheduler_session::load_auth_code()
{
    std::memset(_auth_code, 0, auth_code_size + 1);
    auto const& auth_code_str = _config->worker.auth_code;
    std::memcpy(_auth_code, auth_code_str.c_str(), auth_code_str.size());
}

void scheduler_session::on_config_updated(void* entry)
{
    if (entry == &_config->worker.auth_code)
        post(_worker_session->context(), [this]() -> void {
            load_auth_code();
        });
}

void scheduler_session::clear_worker_tasks(identifier_t identifier)
{
    spdlog::debug(LOG_PREFIX "[{:016x}] Clearing tasks from worker", identifier);
    auto& tasks_by_id = _tasks.get<tasks_by_identifier>();
    tasks_by_id.erase(identifier);
}

void scheduler_session::reset_worker(worker_status* worker)
{
    spdlog::info(LOG_PREFIX "[{:016x}] Resetting worker.", worker->identifier);
    clear_worker_tasks(worker->identifier);
    worker->initialized = false;
    worker->current_connections = 0;
    worker->max_rooms = -1;
    worker->allow_new_task_after = std::chrono::system_clock::now();
    worker->punished = false;
}

void scheduler_session::delete_worker(worker_status* worker)
{
    spdlog::debug(LOG_PREFIX "[{:016x}] Deleting worker.", worker->identifier);
    reset_worker(worker);
    _workers.erase(worker->identifier);
}

void scheduler_session::delete_and_disconnect_worker(worker_status* worker)
{
    auto identifier = worker->identifier;
    spdlog::info(LOG_PREFIX "[{:016x}] Disconnecting worker.", identifier);
    delete_worker(worker);
    _worker_session->disconnect_worker(identifier);
}

template <int N, class Iterator>
Iterator scheduler_session::delete_task(Iterator iter, const bool desc_rank)
{
    auto& tasks_by_id_rid = _tasks.get<N>();
    if (iter == tasks_by_id_rid.end())
    {
        SPDLOG_TRACE(LOG_PREFIX "Failed to delete task: Not found.");
        return iter;
    }
    auto identifier = iter->identifier;
    auto room = iter->room_id;
    spdlog::debug(LOG_PREFIX "[{0:016x}] Deleting task to room {1}. Desc_rank: {2}", identifier, room, desc_rank);
    iter = tasks_by_id_rid.erase(iter);

    auto worker_iter = _workers.find(identifier);
    if (worker_iter != _workers.end())
    {
        if (desc_rank)
        {
            if (!worker_iter->second.punished)
                worker_iter->second.allow_new_task_after = std::chrono::system_clock::now() + std::chrono::minutes(_config->worker.worker_penalty_min);
            else
                worker_iter->second.allow_new_task_after += std::chrono::minutes(_config->worker.worker_penalty_min);  // acc
            //SPDLOG_TRACE(LOG_PREFIX "Updating rank of worker: {}", worker_iter->second.allow_new_task_after.count());
        }
        worker_iter->second.current_connections--;
    }

    auto room_iter = _rooms.find(room);
    if (room_iter != _rooms.end())
        room_iter->second.current_connections--;
    return iter;
}

void scheduler_session::delete_task(identifier_t identifier, room_id_t room_id, bool desc_rank)
{
    SPDLOG_TRACE(LOG_PREFIX "Trying to find and delete task <{0:016x}, {1}>", identifier, room_id);
    auto& tasks_by_id_rid = _tasks.get<tasks_by_identifier_and_room_id>();
    auto task_iter = tasks_by_id_rid.find(boost::make_tuple(identifier, room_id));
    delete_task<tasks_by_identifier_and_room_id>(task_iter, desc_rank);
}

void scheduler_session::assign_task(worker_status* worker, room_status* room, std::chrono::system_clock::time_point now)
{
    auto [iter, inserted] = _tasks.emplace(worker->identifier, room->room_id, now);
    if (!inserted) return;
    SPDLOG_TRACE(LOG_PREFIX "Trying to assign task <{0:016x},{1}>", worker->identifier, room->room_id);
    send_assign(worker->identifier, room->room_id);
    worker->current_connections++;
    room->current_connections++;
    worker->remaining_this_bunch--;
    spdlog::debug(LOG_PREFIX "[{0:016x}] Assigning task to room {1}. N_wk={2}, N_rm={3}", worker->identifier, room->room_id, worker->current_connections, room->current_connections);
}

template <class Container>
int scheduler_session::calculate_max_workers_per_room(Container const& workers_available, int room_count)
{
    if (room_count == 0)
        return 0;
    long long sum = 0;
    for (auto const& [_, worker] : workers_available)
        sum += worker.max_rooms;
    return static_cast<int>(sum / room_count) + 1;
}

void scheduler_session::refresh_counts()
{
    VN_PROFILE_SCOPED(RefreshConnectionCounter)
    auto& tasks_by_wid = _tasks.get<tasks_by_identifier>();
    auto& tasks_by_rid = _tasks.get<tasks_by_room_id>();

    for (auto& [identifier, worker] : _workers)
        worker.current_connections = tasks_by_wid.count(identifier);
    for (auto& [room_id, room] : _rooms)
        room.current_connections = tasks_by_rid.count(room_id);
}

void scheduler_session::check_worker_task_interval()
{
    VN_PROFILE_SCOPED(WorkerTaskIntervalCheck)
    auto now = std::chrono::system_clock::now();
    auto threshold = std::chrono::seconds(_config->worker.worker_timeout_sec);
    for (auto& [_, worker] : _workers)
        if (now - worker.last_received > threshold)
        {
            spdlog::warn(LOG_PREFIX "[{0:016x}] Worker exceeding max interval, disconnecting!", worker.current_connections);
            delete_and_disconnect_worker(&worker);
        }
    for (auto it = _tasks.begin(); it != _tasks.end();)
    {
        auto last_recv = it->last_received;
        if (now - last_recv < threshold)
            ++it;
        else
        {
            auto identifier = it->identifier;
            auto room_id = it->room_id;
            it = delete_task<tasks_by_identifier_and_room_id>(it);
            send_unassign(identifier, room_id);
            spdlog::warn(LOG_PREFIX "[<{0:016x},{1}>] Task exceeding max interval, unassigning!", identifier, room_id);
        }
    }
}

void scheduler_session::send_assign(identifier_t identifier, room_id_t room_id)
{
    SPDLOG_TRACE(LOG_PREFIX "[<{0:016x},{1}>] Sending assign packet.", identifier, room_id);
    auto [buf, siz] = generate_assign_packet(room_id);  // regular unsigned char[]
    send_to_identifier(identifier, buf, siz, unsigned_char_array_deleter);
}

void scheduler_session::send_unassign(identifier_t identifier, room_id_t room_id)
{
    SPDLOG_TRACE(LOG_PREFIX "[<{0:016x},{1}>] Sending unassign packet.", identifier, room_id);
    auto [buf, siz] = generate_unassign_packet(room_id);  // regular unsigned char[]
    send_to_identifier(identifier, buf, siz, unsigned_char_array_deleter);
}

bool room_status_less_tasks(room_status* i, room_status* j)
{
    return i->current_connections < j->current_connections;
}

void scheduler_session::check_all_states()
{
    // Ensure minimum checking interval.
    // This function is costly, so a min interval is required.

    VN_PROFILE_SCOPED(SupervisorCheck)
    auto current_time = std::chrono::system_clock::now();
    auto min_interval = std::chrono::milliseconds(_config->worker.min_check_interval_msec);
    if (current_time - _last_checked < min_interval)
        return;
    _last_checked = current_time;

    SPDLOG_DEBUG(LOG_PREFIX "Triggering check.");

    // 检查最大间隔
    check_worker_task_interval();
    // 刷新所有计数器
    refresh_counts();

    tasks_by_room_id_t& tasks_by_rid = _tasks.get<tasks_by_room_id>();
    // tasks_by_identifier_t& tasks_by_wid = _tasks.get<tasks_by_identifier>(); // unused

    // 先找出所有没有满掉的 worker
    VN_PROFILE_BEGIN(CollectAvailableWorkers)
    auto max_new_per_bunch = _config->worker.max_new_tasks_per_bunch;
    std::vector<worker_status*> workers_available;
    for (auto& [_, worker] : _workers)
        if (worker.current_connections < worker.max_rooms
            && worker.allow_new_task_after < current_time)
        {
            workers_available.push_back(&worker);
            worker.punished = false;
            worker.remaining_this_bunch = max_new_per_bunch;
        }
    //if (workers_available.empty())
    //{
        //spdlog::error(LOG_PREFIX "No available worker!");
        //return;
    //}
    // 按照权值算法排序 worker
    std::sort(workers_available.begin(), workers_available.end(), compare_worker);
    VN_PROFILE_END()

    int max_tasks_per_room = calculate_max_workers_per_room(_workers, _rooms.size());
    max_tasks_per_room = std::max(1, std::max(max_tasks_per_room, static_cast<int>(_workers.size()))); // 确保一个房间至少有1个task，否则就处于 worker 不足状态了
    SPDLOG_TRACE(LOG_PREFIX "Use max t/rm: {}", max_tasks_per_room);

    update_diagnostics(max_tasks_per_room);

    lni::vector<room_status*> room_need_proc; // 需要处理的房间(过多/过少)

    VN_PROFILE_BEGIN(CollectRooms)
    // Delete all inactive rooms
    for (auto it = _rooms.begin(); it != _rooms.end();)
    {
        room_status& room = it->second;
        if (room.active)
        {
            if (room.current_connections != max_tasks_per_room)
                room_need_proc.push_back(&room);
            ++it;
            continue;
        }
        spdlog::info(LOG_PREFIX "Deleting inactive room {0}", room.room_id);
        // disconnect all tasks in room
        auto [begin, end] = tasks_by_rid.equal_range(it->first);
        for (auto task_iter = begin; task_iter != end;
             task_iter = delete_task<tasks_by_room_id>(task_iter))
            send_unassign(task_iter->identifier, task_iter->room_id);
        it = _rooms.erase(it);
    }
    std::sort(room_need_proc.begin(), room_need_proc.end(), room_status_less_tasks);
    VN_PROFILE_END()

    VN_PROFILE_BEGIN(AssignTask)
    for (auto it = room_need_proc.begin(); it != room_need_proc.end(); ++it)
    {
        //SPDLOG_TRACE(LOG_PREFIX "Handling room {0}", it->first);
        room_status& room = **it;
        room_id_t room_id = room.room_id;

        auto overkill = room.current_connections - (max_tasks_per_room + 1); // 房间的 worker 太多了
        if (overkill > 0)
        {
            // Too much workers on one single room. Unassign some.
            spdlog::debug(LOG_PREFIX "Too much workers on room {0}({2}). Try to unassign {1} rooms.", room_id, overkill, room.current_connections);
            auto [begin, end] = tasks_by_rid.equal_range(room_id);
            auto task_iter = begin;
            for (int i = 0; i < overkill && task_iter != end;
                 i++, task_iter = delete_task<tasks_by_room_id>(task_iter, false))
                send_unassign(task_iter->identifier, task_iter->room_id);
        }
        else if (max_tasks_per_room > room.current_connections) // 房间的 worker 不足
        {
            // Not enough workers on this room.
            //spdlog::debug(LOG_PREFIX "Not enough workers on room {0}({2}). Try to assign {1} rooms.", room_id, underkill, room.current_connections);
            auto worker_iter = workers_available.begin();
            if (worker_iter == workers_available.end())
                continue;
            auto worker = *worker_iter;
            if (worker->current_connections > worker->max_rooms || worker->remaining_this_bunch <= 0)
            {
                workers_available.erase(worker_iter);
                continue;
            }
            assign_task(worker, &room, current_time);
        }

        //if (room.current_connections < 1)
            //spdlog::warn(LOG_PREFIX "Room {0} can't get any worker!", room_id);
    }

    VN_PROFILE_END()
}

void scheduler_session::update_diagnostics(int max_tasks_per_room)
{
    VN_PROFILE_SCOPED(UpdateDiagnostics)
    _diag_context.update_diagnostics(_rooms, _workers, _tasks, max_tasks_per_room);
    _diag_data_handler(_diag_context.data(), _diag_context.size());
}

void scheduler_session::handle_new_worker(
    identifier_t identifier)
{
    auto current_time = std::chrono::system_clock::now();
    auto worker_iter = _workers.find(identifier);
    worker_status* worker_ptr = worker_iter != _workers.end()
                                    ? &(worker_iter->second)
                                    : nullptr;
    spdlog::info(LOG_PREFIX "[{0:016x}] New worker connected.", identifier);
    if (worker_ptr != nullptr)
    {
        // worker already existing. Reset the worker.
        //clear_worker(*worker_ptr);
        reset_worker(worker_ptr);
        worker_ptr->last_received = current_time;
        return;
    }

    // Allocate new worker.
    auto [worker_allocated_iter, allocated] = _workers.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(identifier),
        std::forward_as_tuple(identifier, current_time));
    assert(allocated);
}

// Run in ZeroMQ Worker thread.
void scheduler_session::handle_buffer(
    identifier_t identifier, unsigned char* payload_data,
    size_t payload_len)
{
    if (payload_len < room_failed_payload_length)
        return; // Malformed
    VN_PROFILE_SCOPED(HandleWorkerBuffer)
    auto op_code = payload_data[0]; // data[0]
    room_id_t room_id = boost::asio::detail::socket_ops::network_to_host_long(
        *reinterpret_cast<simple_message_header*>(payload_data + 1)); // data[1,2,3,4]

    SPDLOG_TRACE(LOG_PREFIX "[{0:016x}] Worker message: op_code={1}, rid/rmax={2}", identifier, op_code, room_id);

    auto worker_iter = _workers.find(identifier);
    worker_status* worker_ptr = worker_iter != _workers.end()
                                    ? &(worker_iter->second)
                                    : nullptr;

    if (worker_ptr == nullptr)
    {
        spdlog::warn(LOG_PREFIX "[{0:016x}] Worker status not found with this identifier. Disconnecting.", identifier);
        clear_worker_tasks(identifier);
        _worker_session->disconnect_worker(identifier);
        return;
    }

    auto current_time = std::chrono::system_clock::now();
    if (worker_ptr)
        worker_ptr->last_received = current_time;

    if (op_code == worker_ready_code)
    {
        if (payload_len < worker_ready_payload_length
            || 0 != std::strncmp(_auth_code, reinterpret_cast<char*>(payload_data) + 5, auth_code_size))
        {
            spdlog::info(LOG_PREFIX "[{:016x}] Auth failed. Disconnecting worker.", identifier);
            delete_worker(worker_ptr);
            _worker_session->disconnect_worker(identifier);
        }
        // see simple_worker_proto.h
        auto max_rooms = room_id; // max_rooms is in the place of room_id
        spdlog::info(LOG_PREFIX "[{0:016x}] Worker ready. rmax={1}", identifier, max_rooms);
        // Reset worker.
        reset_worker(worker_ptr);
        worker_ptr->initialized = true;
        worker_ptr->max_rooms = max_rooms;
        check_all_states();
    }
    else if (op_code == room_failed_code)
    {
        spdlog::warn(LOG_PREFIX "[<{0:016x},{1}>] Task failed.", identifier, room_id);
        delete_task(identifier, room_id);
        check_all_states();
    }
    else if (op_code == worker_data_code)
    {
        if (payload_len < worker_data_payload_header_length)  // 1 + 4 + 4 + 32
        {
            SPDLOG_TRACE(LOG_PREFIX "Malformed data packet: wrong payload len {}!=33!", payload_len);
            return;
        }

        tasks_by_identifier_and_room_id_t& idx = _tasks.get<0>();
        auto task_iter = idx.find(boost::make_tuple(identifier, room_id));
        if (task_iter == idx.end())
            return;

        idx.modify(task_iter, [current_time](room_task& it) -> void
        {
            it.last_received = current_time;
        });
        auto crc32 = *reinterpret_cast<checksum_t*>(payload_data + 5);

        if (crc32 == 0)
        {
            auto room_iter = _rooms.find(room_id);
            if (room_iter == _rooms.end())
                return;
            auto& room = room_iter->second;
            if ((current_time - room.last_empty_crc_received) < std::chrono::seconds(_config->message.min_interval_popularity_sec))
                return;
            room.last_empty_crc_received = current_time;
        }

        auto routing_key = reinterpret_cast<char*>(payload_data) + 9;
        auto routing_key_len = strnlen(routing_key, routing_key_max_size);
        spdlog::debug(LOG_PREFIX "[<{0:016x},{1}>] Received data packet. payload_len={2}, CRC32={3}", identifier, room_id, payload_len - 33, crc32);

        _data_handler(
            crc32,
            std::string_view(routing_key, routing_key_len),
            reinterpret_cast<unsigned char*>(payload_data) + 33,
            payload_len - 33);
    }
}

void scheduler_session::handle_worker_disconnect(identifier_t identifier)
{
    spdlog::warn(LOG_PREFIX "[{0:016x}] Worker disconnected. Deleting.", identifier);
    clear_worker_tasks(identifier);
    auto iter = _workers.find(identifier);
    if (iter != _workers.end())
        delete_worker(&(iter->second));
}

void scheduler_session::send_to_identifier(
    const uint64_t identifier, unsigned char* payload, const size_t size, supervisor_buffer_deleter deleter)
{
    _worker_session->send_message(identifier, payload, size, deleter);
}
}
