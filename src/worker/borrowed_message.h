#pragma once

namespace vNerve
{
class borrowed_message
{
public:
    int crc32;
    // TODO: use size constant from shared folder
    char routing_key[24];
    virtual size_t size() = 0;
    virtual void write(void* data, int size) = 0;
};
}  // namespace vNerve
