#pragma once

#include "simple_worker_proto.h"

namespace vNerve::bilibili
{
class borrowed_message
{
public:
    int crc32;
    char routing_key[worker_supervisor::routing_key_max_size];
    virtual size_t size() = 0;
    virtual void write(void* data, int size) = 0;

    virtual ~borrowed_message() = default;
};
}  // namespace vNerve
