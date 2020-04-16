#pragma once

class borrowed_buffer
{
public:
    virtual size_t size() = 0;
    virtual void write(void* data, int size) = 0;
};
