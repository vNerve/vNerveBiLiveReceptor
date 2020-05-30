#pragma once

#include <cstdint>

namespace vNerve::bilibili
{
namespace worker_supervisor
{
using identifier_t = uint64_t;
using room_id_t = int;
}
using checksum_t = int; // CRC-32
}