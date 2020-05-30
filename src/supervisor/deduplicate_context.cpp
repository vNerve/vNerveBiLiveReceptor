#include "deduplicate_context.h"

namespace vNerve::bilibili
{
bool deduplicate_context::check_and_add(checksum_t checksum, std::chrono::system_clock::time_point add_time)
{
    auto [_, added] = _container.emplace_back(checksum, add_time);
    return added;
}

void deduplicate_context::check_expire(const std::chrono::system_clock::time_point now)
{
    auto exp = now - _threshold;
    auto& container_seq = _container.get<0>();
    for (
        auto it = container_seq.begin();
        it != container_seq.end() && it->add_time < exp;
        it = container_seq.erase(it))
        ;
}
}