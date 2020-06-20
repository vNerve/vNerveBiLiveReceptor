#pragma once
#include "type.h"

#include <utility>
#include <functional>

namespace vNerve::bilibili::worker_supervisor
{
using simple_message_header = unsigned long;
const size_t identifier_size = 8;
const size_t routing_key_max_size = 24;
const size_t auth_code_size = 32;

inline const unsigned char worker_ready_code = static_cast<unsigned char>(0x00000001);
inline const unsigned char room_failed_code =  static_cast<unsigned char>(0x00000002);
inline const unsigned char worker_data_code =  static_cast<unsigned char>(0x00000000);

inline const unsigned char assign_room_code =   static_cast<unsigned char>(0x10000001);
inline const unsigned char unassign_room_code = static_cast<unsigned char>(0x10000002);

inline const size_t simple_message_header_length = sizeof(unsigned int);
inline const size_t crc_32_length = sizeof(checksum_t);
inline const size_t room_id_length = sizeof(room_id_t);
inline const unsigned int worker_ready_payload_length = 1 + room_id_length + auth_code_size;
inline const unsigned int room_failed_payload_length = 1 + room_id_length;
inline const unsigned int assign_unassign_payload_length = 1 + room_id_length;
inline const unsigned int worker_data_payload_header_length = 1 + room_id_length + crc_32_length + routing_key_max_size;

/*
 * All big endian.
 * byte    uint32
 * OP_CODE=1 ROOM_ID   (ROOM FAILED)
 * OP_CODE=2 MAX_ROOMS AUTHCODE[32] (WORKER READY)
 *
 * byte      uint32  int32 char[24]
 * OP_CODE=0 ROOM_ID CRC32 ROUTING_KEY PAYLOAD
 *
 * OP_CODE ROOM_ID
 */

/**
 * All packets starts with packet length, then the payload.
 */

using buffer_handler = std::function<void (unsigned char*, size_t)>;
///
/// 用于处理一次读取获得的缓冲区。
/// 一次缓冲区可能不完整或包含多个数据包。本函数可以处理此种情况。
/// 本函数断言 *buf* 的最开始为一个完整的数据包头部。
/// @param buf *整个*缓冲区
/// @param transferred 本次读取到的字节数
/// @param buffer_size 整个缓冲区的大小
/// @param skipping_size 上次调用获得的返回值的第二项，标识应该跳过的大小
/// @param handler 回调，用于处理获取到的信息
/// @return 下次读取结果应该存放的偏移量以及需要传入下一次调用最后一个参数的偏移量。如果本结果含有不完整的数据包，本函数将会将该数据包的一部分复制到 `buf` 开头，则返回的就是数据包片段的尾部位置 + 1.
std::pair<size_t, size_t> handle_simple_message(unsigned char* buf, size_t transferred,
                                                size_t buffer_size,
                                                size_t skipping_size,
                                                buffer_handler handler);
}
