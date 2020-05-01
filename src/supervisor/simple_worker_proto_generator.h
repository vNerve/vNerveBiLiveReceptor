#pragma once
#include "simple_worker_proto.h"
#include "type.h"

namespace vNerve::bilibili::worker_supervisor
{
///
/// Use delete[] to remove!
std::pair<unsigned char*, size_t> generate_unassign_packet(room_id_t room_id);
std::pair<unsigned char*, size_t> generate_assign_packet(room_id_t room_id);
}
