#pragma once

namespace vNerve::bilibili::worker_supervisor
{
const size_t identifier_size = 8;
const size_t routing_key_max_size = 24;

inline const unsigned char worker_ready_code = 0x00000001;
inline const unsigned char room_failed_code = 0x00000002;
inline const unsigned char worker_data_code = 0x00000000;

inline const unsigned char assign_room_code = 0x10000001;
inline const unsigned char unassign_room_code = 0x10000002;

/*
 * All big endian.
 * byte    uint32
 * OP_CODE=1 ROOM_ID   (ROOM FAILED)
 * OP_CODE=2 MAX_ROOMS (WORKER READY)
 *
 * byte      uint32  int32 char[24]
 * OP_CODE=0 ROOM_ID CRC32 ROUTING_KEY PAYLOAD
 *
 * OP_CODE ROOM_ID
 */
}
