#pragma once

#include "borrowed_message.h"

namespace vNerve::bilibili
{
// 我寻思这里该写点文档
const borrowed_message* serialize_buffer(char* buf, const size_t& length, const unsigned int& roomid);
}  // namespace vNerve::bilibili
