#pragma once
#include <robin_hood.h>
#include <string>
#include <string_view>

namespace vNerve::bilibili::util
{
struct string_view_cmp
{
    using is_transparent = void;
    bool operator()(std::string_view const& a, std::string_view const& b) const { return a == b; }
    bool operator()(std::string const& a, std::string_view const& b) const { return a == b; }
    bool operator()(std::string const& a, std::string const& b) const { return a == b; }
    bool operator()(std::string_view const& a, std::string const& b) const { return a == b; }
};

struct string_view_hash
{
    using is_transparent = void;
    size_t operator()(const std::string& str) const { return robin_hood::hash_bytes(str.c_str(), str.size()); }
    size_t operator()(const std::string_view& str) const { return robin_hood::hash_bytes(str.data(), str.size()); }
};

template <class Element>
using unordered_map_string = robin_hood::unordered_map<std::string, Element, string_view_hash, string_view_cmp>;
}
