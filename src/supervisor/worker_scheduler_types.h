#pragma once

#include "type.h"

#include <functional>
#include <chrono>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <robin_hood.h>

namespace vNerve::bilibili::worker_supervisor
{
struct worker_status;
struct room_status;

struct room_task
{
    identifier_t identifier;
    room_id_t room_id;

    std::chrono::system_clock::time_point last_received;
    //std::weak_ptr<worker_status> worker; // is use shared_ptr + weak_ptr better than looking up unordered_map?
    //std::weak_ptr<room_status> room;

    room_task(identifier_t identifier, room_id_t room_id, std::chrono::system_clock::time_point now)
        : identifier(identifier), room_id(room_id), last_received(now) {}
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

    ///
    /// 用于断线惩罚。
    std::chrono::system_clock::time_point allow_new_task_after;
    ///
    /// 用于判断是将断线惩罚累加到 allow_new_task_after 还是从当前时间开始计算。
    bool punished = false;

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

using supervisor_data_handler = std::function<void(checksum_t, std::string_view, unsigned char const*, size_t)>;
using supervisor_diag_data_handler = std::function<void(unsigned char const*, size_t)>;
using supervisor_server_tick_handler = std::function<void()>;

using simple_buffer = std::pair<std::unique_ptr<unsigned char[]>, size_t>;
template <typename Key, typename Value>
using unordered_map = robin_hood::unordered_map<Key, Value>;
template <typename Element>
using unordered_set = robin_hood::unordered_set<Element>;

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
}
