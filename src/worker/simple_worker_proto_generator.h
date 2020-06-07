#pragma once

#include "type.h"

#include <utility>

namespace vNerve {
namespace bilibili {
class borrowed_message;
}
}

namespace vNerve::bilibili::worker_supervisor
{
///
/// Use delete[] to remove!
std::pair<unsigned char*, size_t> generate_room_failed_packet(room_id_t room_id);
std::pair<unsigned char*, size_t> generate_worker_ready_packet(int max_rooms);
}  // namespace vNerve::bilibili::worker_supervisor