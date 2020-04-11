#pragma once
#include "config.h"

#include <boost/asio/detail/socket_ops.hpp>

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

class supervisor_server_session;
class room_session;

class worker_session
{
private:
    uint64_t _identifier;
    unsigned int _max_rooms;
    bool _initialized = false;

    unordered_map<uint32_t, std::weak_ptr<room_session>> _connect_rooms;
    std::chrono::system_clock::time_point _last_received;

public:
    worker_session(uint64_t identifier)
        : _identifier(identifier), _max_rooms(0)
    {
    }
    ~worker_session();

    void initialize_update_max_rooms(const unsigned int max_rooms)
    {
        _initialized = true;
        _max_rooms = max_rooms;
    }

    void update_time(const std::chrono::system_clock::time_point& ts)
    {
        _last_received = ts;
    }

    [[nodiscard]] bool is_initialized() const
    {
        return _initialized;
    }

    [[nodiscard]] std::chrono::system_clock::time_point get_last_received() const
    {
        return _last_received;
    }

    /**
     * Clear all rooms on the worker.
     * Will remove worker info on the associated rooms.
     * Will not send UNASSIGN packet.
     */
    void clear_all_rooms_no_notify();
    /**
     * Remove one room on the worker.
     * Will remove worker info on the associated room.
     * Will send UNASSIGN packet.
     */
    void remove_room(uint32_t);
    /**
     * Remove one room on the worker.
     * Will remove worker info on the associated room.
     * Will not send UNASSIGN packet.
     */
    void remove_room_no_notify(uint32_t);

    /**
     * Reset the worker to uninitialized state and clear all rooms.
     */
    void reset()
    {
        clear_all_rooms_no_notify();
        _initialized = false;
    }
};

class room_session
{
private:
    uint32_t _room_id;

    unordered_map<uint64_t, std::pair<std::weak_ptr<worker_session>,
                                      std::chrono::system_clock::time_point>>
        _connected_workers_and_last_time;

    std::shared_ptr<worker_session> remove_worker_and_get(uint64_t identifier);

public:
    void update_time(uint64_t identifier,
                     const std::chrono::system_clock::time_point&);
    /**
     * Remove one worker on the room.
     * Will not remove room info on the worker.
     */
    void remove_worker(uint64_t identifier);
    /**
     * Remove one worker on the room.
     * Will remove room info on the worker.
     * Will send UNASSIGN packet.
     */
    void remove_worker_both_side(uint64_t identifier);
    /**
     * Remove one worker on the room.
     * Will remove room info on the worker.
     * Will not send UNASSIGN packet.
     */
    void remove_worker_both_side_no_notify(uint64_t identifier);
};

class scheduler_session : std::enable_shared_from_this<scheduler_session>
{
private:
    std::shared_ptr<worker_supervisor::supervisor_server_session> _worker_session;

    unordered_map<uint64_t, worker_session> _worker_sessions; // 你跟得上我的思必得吗？
    unordered_map<uint32_t, room_session> _room_sessions;

    config::config_t _config;

    std::chrono::system_clock::time_point _last_checked;
    std::chrono::system_clock::duration _min_check_interval;
    void check_all_states();
    void check_task_intervals();
    void check_rooms();
    /**
     * Called when a new worker connected but didn't sent WORKER_READY packet yet.
     */
    void handle_new_worker(uint64_t identifier);
    void handle_buffer(unsigned long long identifier, unsigned char* payload_data, size_t payload_len);
    void send_to_identifier(uint64_t identifier, unsigned char* payload,
                            size_t size);

public:
    scheduler_session(config::config_t config);
    ~scheduler_session();
};
}  // namespace worker_supervisor
}
