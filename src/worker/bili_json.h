#pragma once

#include "borrowed_message.h"

namespace vNerve::bilibili
{
// 我寻思这里该写点文档
/// @param buf 以\0结尾的JSON字符串
/// @param length 字符串长度
/// @param roomid 房间号
const borrowed_message* serialize_buffer(char* buf, const size_t& length, const unsigned int& room_id);
const borrowed_message* serialize_popularity(const long long popularity, const unsigned int& room_id);
}  // namespace vNerve::bilibili
