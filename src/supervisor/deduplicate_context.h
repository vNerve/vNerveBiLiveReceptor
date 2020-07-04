#pragma once

#include "type.h"

#include <chrono>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

namespace vNerve::bilibili
{
struct deduplicate_entry
{
    checksum_t value;
    std::chrono::system_clock::time_point add_time;

    deduplicate_entry(const checksum_t value, const std::chrono::system_clock::time_point& add_time)
        : value(value),
          add_time(add_time)
    {
    }
};

class deduplicate_context
{
private:
    using deduplicate_container =
        boost::multi_index_container<
            deduplicate_entry,
            boost::multi_index::indexed_by<
                boost::multi_index::sequenced<>,
                boost::multi_index::hashed_unique<boost::multi_index::member<deduplicate_entry, checksum_t, &deduplicate_entry::value>>>>;
    deduplicate_container _container;

    int* _threshold_sec_ptr;

public:
    deduplicate_context(int* threshold_sec)
        : _threshold_sec_ptr(threshold_sec) {}

    bool check_and_add(const checksum_t checksum) { return check_and_add(checksum, std::chrono::system_clock::now()); }
    bool check_and_add(checksum_t checksum, std::chrono::system_clock::time_point add_time);

    void check_expire() { check_expire(std::chrono::system_clock::now()); }
    void check_expire(std::chrono::system_clock::time_point now);

    [[nodiscard]] size_t size() const { return _container.size(); }

    deduplicate_context(const deduplicate_context& other) = delete;
    deduplicate_context(deduplicate_context&& other) noexcept
        : _container(std::move(other._container)),
          _threshold_sec_ptr(other._threshold_sec_ptr)
    {
    }

    deduplicate_context& operator=(const deduplicate_context& other) = delete;
    deduplicate_context& operator=(deduplicate_context&& other) noexcept
    {
        if (this == &other)
            return *this;
        _container = std::move(other._container);
        _threshold_sec_ptr = other._threshold_sec_ptr;
        return *this;
    }
};
}