#include "worker_scheduler.h"

#include "supervisor_server_session.h"
#include "simple_worker_proto.h"

namespace vNerve::bilibili::worker_supervisor
{
// =============================== worker_session ===============================

worker_session::~worker_session() {}

void worker_session::
    clear_all_rooms_no_notify()
{
    for (auto it = _connect_rooms.begin(); it != _connect_rooms.end();
         it = _connect_rooms.erase(it)) // <-------- ERASING!
    {
        std::shared_ptr<room_session> room_ptr = it->second.lock();
        room_ptr->remove_worker(_identifier);
    }
}

void worker_session::remove_room(
    uint32_t room_id)
{
    remove_room_no_notify(room_id);
    // todo send unassign
}

void worker_session::remove_room_no_notify(uint32_t room_id)
{
    auto iter = _connect_rooms.find(room_id);
    if (iter == _connect_rooms.end())
        return;
    std::weak_ptr<room_session> room_ptr = iter->second;
    if (room_ptr.expired())
        return;
    room_ptr.lock()->remove_worker(_identifier);
}

// =============================== room_session ===============================

std::shared_ptr<worker_session> room_session::remove_worker_and_get(
        uint64_t identifier)
{
    auto iter = _connected_workers_and_last_time.find(identifier);
    if (iter == _connected_workers_and_last_time.end())
        return std::shared_ptr<worker_session>();
    std::weak_ptr<worker_session> worker_ptr = iter->second.first;
    _connected_workers_and_last_time.erase(iter);
    return worker_ptr.lock();
}

void room_session::update_time(
    uint64_t identifier, const std::chrono::system_clock::time_point& ts)
{
    auto worker_iter = _connected_workers_and_last_time.find(identifier);
    if (worker_iter == _connected_workers_and_last_time.end())
        return;
    worker_iter->second.second = ts;
}

void room_session::remove_worker(
    uint64_t identifier)
{
    _connected_workers_and_last_time.erase(identifier);
}

void room_session::remove_worker_both_side(
    uint64_t identifier)
{
    auto worker = remove_worker_and_get(identifier);
    if (worker)
        worker->remove_room(_room_id);
}

void room_session::
    remove_worker_both_side_no_notify(uint64_t identifier)
{
    auto worker = remove_worker_and_get(identifier);
    if (worker)
        worker->remove_room_no_notify(_room_id);
}

// =============================== scheduler_session ===============================

scheduler_session::scheduler_session(const config::config_t config)
    : _config(config),
      _min_check_interval(
          std::chrono::milliseconds(
              (*config)["min-check-interval-ms"].as<int>()))
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

// Run in ZeroMQ Worker thread.
void scheduler_session::check_all_states()
{
    auto current_time = std::chrono::system_clock::now();
    if (current_time - _last_checked < _min_check_interval)
        return;
    _last_checked = current_time;

    check_task_intervals();
    check_rooms();
}

void scheduler_session::check_task_intervals()
{

}

void scheduler_session::check_rooms()
{

}

void scheduler_session::handle_new_worker(
    uint64_t identifier)
{
    auto current_time = std::chrono::system_clock::now();
    auto worker_iter = _worker_sessions.find(identifier);
    auto worker_ptr = worker_iter != _worker_sessions.end()
                          ? &(worker_iter->second)
                          : nullptr;
    if (worker_ptr != nullptr)
    {
        worker_ptr->reset();
        worker_ptr->update_time(current_time);
        return;
    }

    // Allocate new worker.
    auto [worker_allocated_iter, allocated] = _worker_sessions.emplace(
        std::piecewise_construct, std::forward_as_tuple(identifier),
        std::forward_as_tuple(identifier));
    assert(allocated);
    worker_ptr = &worker_allocated_iter->second;
    worker_ptr->update_time(current_time);
    check_all_states();
    // TODO assign tasks!
}

// Run in ZeroMQ Worker thread.
void scheduler_session::handle_buffer(
    unsigned long long identifier, unsigned char* payload_data,
    size_t payload_len)
{
    if (payload_len < 5)
    {
        //todo log?
        return;
    }
    auto op_code = payload_data[0];  // data[0]
    auto room_id = boost::asio::detail::socket_ops::network_to_host_long(
        *reinterpret_cast<unsigned long*>(payload_data + 1));  // data[1,2,3,4]

    auto worker_iter = _worker_sessions.find(identifier);
    auto worker_ptr = worker_iter != _worker_sessions.end()
                          ? &(worker_iter->second)
                          : nullptr;

    auto current_time = std::chrono::system_clock::now();
    if (worker_ptr)
        worker_ptr->update_time(current_time);

    if (op_code == worker_ready_code)
    {
        // see simple_worker_proto.h
        auto max_rooms = room_id;  // max_rooms is in the place of room_id
        if (!worker_ptr)
            return
                // Reset worker.
                worker_ptr->clear_all_rooms_no_notify();
        worker_ptr->initialize_update_max_rooms(max_rooms);
        check_all_states();
        // TODO reassign tasks!
    }
    else if (op_code == room_failed_code)
    {
        if (worker_ptr)
            worker_ptr->remove_room_no_notify(room_id);
        // remove_room_no_notify will do this
        /*auto room_iter = _room_sessions.find(room_id);
        if (room_iter != _room_sessions.end())
            room_iter->second.remove_worker(identifier);*/
        check_all_states();
        // TODO reassign failed room!
    }
    else if (op_code == worker_data_code)
    {
        if (payload_len < 33)  // 1 + 4 + 4 + 24
        {
            return;
            // TODO:Log
        }

        auto room_iter = _room_sessions.find(room_id);
        auto room_ptr =
            room_iter != _room_sessions.end() ? &(room_iter->second) : nullptr;
        if (!room_ptr)
        {
            if (worker_ptr)
                worker_ptr->remove_room(room_id);
            return;
        }
        if (!worker_ptr)
        {
            room_ptr->remove_worker(identifier);
            return;
        }
        room_ptr->update_time(identifier, current_time);

        auto crc32 = *reinterpret_cast<unsigned long*>(payload_data + 5);
        auto routing_key = reinterpret_cast<char*>(payload_data) + 9;
        auto routing_key_len = strnlen(routing_key, routing_key_max_size);

        // TODO send out packet to MQ
    }
}

void scheduler_session::send_to_identifier(
    uint64_t identifier, unsigned char* payload, const size_t size)
{
    _worker_session->send_message(identifier, payload, size);
}
}
