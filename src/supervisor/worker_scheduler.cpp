#include "worker_scheduler.h"

#include "supervisor_server_session.h"
#include "simple_worker_proto.h"
#include "simple_worker_proto_generator.h"

#include <algorithm>
#include <boost/range/adaptors.hpp>

namespace vNerve::bilibili::worker_supervisor
{
supervisor_buffer_deleter unsigned_char_array_deleter = [](unsigned char* buf) -> void {
    delete[] buf;
};

bool compare_worker(const worker_status* lhs, const worker_status* rhs)
{
    // TODO weight algorithm base on rank and remaining room;
    return lhs->rank * (lhs->max_rooms - lhs->current_connections) > rhs->rank * (rhs->max_rooms - rhs->current_connections);
}

// =============================== scheduler_session ===============================

scheduler_session::scheduler_session(const config::config_t config)
    : _config(config),
      _min_check_interval(
          std::chrono::milliseconds(
              (*config)["min-check-interval-ms"].as<int>())),
      _worker_interval_threshold(std::chrono::seconds((*config)["worker-interval-threshold-sec"].as<int>()))
{
    _worker_session = std::make_shared<supervisor_server_session>(
        config,
        std::bind(&scheduler_session::handle_buffer, shared_from_this(),
                  std::placeholders::_1, std::placeholders::_2,
                  std::placeholders::_3),
        std::bind(&scheduler_session::check_all_states, shared_from_this()),
        std::bind(&scheduler_session::handle_new_worker, shared_from_this(),
                  std::placeholders::_1));
}

scheduler_session::~scheduler_session() {}

void scheduler_session::clear_worker_tasks(identifier_t identifier)
{
    auto& tasks_by_id = _tasks.get<tasks_by_identifier>();
    tasks_by_id.erase(identifier);
}

void scheduler_session::reset_worker(worker_status* worker)
{
    clear_worker_tasks(worker->identifier);
    worker->initialized = false;
    worker->current_connections = 0;
    worker->max_rooms = -1;
}

void scheduler_session::delete_worker(worker_status* worker)
{
    reset_worker(worker);
    _workers.erase(worker->identifier);
}

void scheduler_session::delete_and_disconnect_worker(worker_status* worker)
{
    delete_worker(worker);
    _worker_session->disconnect_worker(worker->identifier);
}

tasks_set::iterator scheduler_session::delete_task(tasks_set::iterator iter)
{
    auto& tasks_by_id_rid = _tasks.get<tasks_by_identifier_and_room_id>();
    if (iter == tasks_by_id_rid.end())
        return iter;
    auto identifier = iter->identifier;
    iter = tasks_by_id_rid.erase(iter);

    auto& worker_iter = _workers.find(identifier);
    if (worker_iter == _workers.end())
        return iter;
    // TODO better rank algorithm
    worker_iter->second.rank -= 1.0;
    return iter;
}

void scheduler_session::delete_task(identifier_t identifier, room_id_t room_id)
{
    auto& tasks_by_id_rid = _tasks.get<tasks_by_identifier_and_room_id>();
    auto task_iter = tasks_by_id_rid.find(boost::make_tuple(identifier, room_id));
    delete_task(task_iter);
}

void scheduler_session::delete_task_unassign(identifier_t identifier, room_id_t room_id)
{
    delete_task(identifier, room_id);
    send_unassign(identifier, room_id);
}

void scheduler_session::refresh_counts()
{
    auto& tasks_by_wid = _tasks.get<tasks_by_identifier>();
    auto& tasks_by_rid = _tasks.get<tasks_by_room_id>();

    for (auto& [identifier, worker] : _workers)
        worker.current_connections = tasks_by_wid.count(identifier);
    for (auto& [room_id, room] : _rooms)
        room.current_connections = tasks_by_rid.count(room_id);
}

void scheduler_session::check_worker_task_interval()
{
    auto now = std::chrono::system_clock::now();
    for (auto& [_, worker] : _workers)
        if (now - worker.last_received > _worker_interval_threshold)
            delete_and_disconnect_worker(&worker);
    for (auto it = _tasks.begin(); it != _tasks.end();)
        if (now - it->last_received <= _worker_interval_threshold)
            ++it;
        else
            it = delete_task(it);
}

// Run in ZeroMQ Worker thread.
void scheduler_session::check_all_states()
{
    auto current_time = std::chrono::system_clock::now();
    if (current_time - _last_checked < _min_check_interval)
        return;
    _last_checked = current_time;

    check_worker_task_interval();
    refresh_counts();
    // TODO 按时间实现回复worker->rank

    std::vector<worker_status*> workers_available;
    for (auto& [_, worker] : _workers)
        if (worker.current_connections < worker.max_rooms)
            workers_available.push_back(&worker);

    std::sort(workers_available.begin(), workers_available.end(), compare_worker);

    for (auto& [room_id, room] : _rooms)
    {

    }
}

void scheduler_session::send_assign(identifier_t identifier, room_id_t room_id)
{
    auto [buf, siz] = generate_assign_packet(room_id);  // regular unsigned char[]
    send_to_identifier(identifier, buf, siz, unsigned_char_array_deleter);
}

void scheduler_session::send_unassign(identifier_t identifier, room_id_t room_id)
{
    auto [buf, siz] = generate_unassign_packet(room_id);  // regular unsigned char[]
    send_to_identifier(identifier, buf, siz, unsigned_char_array_deleter);
}

void scheduler_session::handle_new_worker(
    identifier_t identifier)
{
    auto current_time = std::chrono::system_clock::now();
    auto worker_iter = _workers.find(identifier);
    worker_status* worker_ptr = worker_iter != _workers.end()
                          ? &(worker_iter->second)
                          : nullptr;
    if (worker_ptr != nullptr)
    {
        // worker already existing. Reset the worker.
        //clear_worker(*worker_ptr);
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
    if (payload_len < 5)
    {
        //todo log?
        return;
    }
    auto op_code = payload_data[0];  // data[0]
    room_id_t room_id = boost::asio::detail::socket_ops::network_to_host_long(
        *reinterpret_cast<simple_message_header*>(payload_data + 1));  // data[1,2,3,4]

    auto worker_iter = _workers.find(identifier);
    worker_status* worker_ptr = worker_iter != _workers.end()
                          ? &(worker_iter->second)
                          : nullptr;

    if (worker_ptr == nullptr)
    {
        clear_worker_tasks(identifier);
        _worker_session->disconnect_worker(identifier);
        return;
    }

    auto current_time = std::chrono::system_clock::now();
    if (worker_ptr)
        worker_ptr->last_received = current_time;

    if (op_code == worker_ready_code)
    {
        // see simple_worker_proto.h
        auto max_rooms = room_id;  // max_rooms is in the place of room_id
        // Reset worker.
        reset_worker(worker_ptr);
        worker_ptr->initialized = true;
        worker_ptr->max_rooms = max_rooms;
        check_all_states();
    }
    else if (op_code == room_failed_code)
    {
        delete_task(identifier, room_id);
        check_all_states();
    }
    else if (op_code == worker_data_code)
    {
        if (payload_len < 33)  // 1 + 4 + 4 + 24
        {
            return;
            // TODO:Log
        }
        // TODO update some "weight" of the worker

        tasks_by_identifier_and_room_id_t& idx = _tasks.get<0>();
        auto task_iter = idx.find(boost::make_tuple(identifier, room_id));
        if (task_iter == idx.end())
            return;

        idx.modify(task_iter, [current_time](room_task& it) -> void {
            it.last_received = current_time;
        });
        auto crc32 = *reinterpret_cast<unsigned long*>(payload_data + 5);
        auto routing_key = reinterpret_cast<char*>(payload_data) + 9;
        auto routing_key_len = strnlen(routing_key, routing_key_max_size);

        // TODO send out packet to MQ
    }
}

void scheduler_session::send_to_identifier(
    const uint64_t identifier, unsigned char* payload, const size_t size, supervisor_buffer_deleter deleter)
{
    _worker_session->send_message(identifier, payload, size, deleter);
}
}
