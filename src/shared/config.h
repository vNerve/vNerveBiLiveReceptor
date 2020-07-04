#pragma once

#include "robin_hood_ext.h"

#include <boost/program_options.hpp>
#include <memory>
#include <list>
#include <functional>
#include <spdlog/spdlog.h>

namespace vNerve::bilibili
{
namespace config
{
using config_t = std::shared_ptr<boost::program_options::variables_map>;
using config_listener = std::function<void(void*)>;

class config_dynamic_linker
{
private:
    util::unordered_map_string<std::pair<bool, int*>> _int_entries;
    util::unordered_map_string<std::pair<bool, std::string*>> _string_entries;
    robin_hood::unordered_map<void*, config_listener> _listeners;

    void fire_config_changed(void* target)
    {
        for (auto& [_, func] : _listeners)
            func(target);
    }

public:
    config_dynamic_linker() {}

    void update_config(std::string_view name, std::string const& raw_value)
    {
        auto str_entry_iter = _string_entries.find(name);
        if (str_entry_iter == _string_entries.end())
        {
            auto int_entry_iter = _int_entries.find(name);
            if (int_entry_iter == _int_entries.end())
                return spdlog::error("[config] No config entry named {} found!", name);
            if (!int_entry_iter->second.first)
                return spdlog::error("[config] Config {} is not available for dynamic updating. Please shut down server and change it in the config file.", name);

            try
            {
                auto ptr = int_entry_iter->second.second;
                auto value = std::stoi(raw_value);
                *ptr = value;
                fire_config_changed(ptr);
                spdlog::info("[config] Changed config {} to {}.", name, value);
            }
            catch (std::exception& ex)
            {
                return spdlog::error("[config] Invalid input {}: {}", raw_value, ex.what());
            }
            return;
        }
        if (!str_entry_iter->second.first)
            return spdlog::error("[config] Config {} is not available for dynamic updating. Please shut down server and change it in the config file.", name);

        auto ptr = str_entry_iter->second.second;
        *ptr = raw_value;
        fire_config_changed(ptr);
        spdlog::info("[config] Changed config {} to {}.", name, raw_value);
    }

    void register_entry(std::string const& name, int* target, bool enabled)
    {
        _int_entries.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(enabled, target));
    }

    void register_entry(std::string const& name, std::string* target, bool enabled)
    {
        _string_entries.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(enabled, target));
    }

    void register_listener(void* src, config_listener&& listener)
    {
        _listeners.emplace(src, std::move(listener));
    }

    void unregister_listener(void* src)
    {
        _listeners.erase(src);
    }
};

using config_linker_t = std::shared_ptr<config_dynamic_linker>;

boost::program_options::options_description create_description();

///
/// Parse options from command line arguments and file.
/// @return Parsed variable. Null when the process should be terminated.
config_t parse_options(int argc, char** argv);


}  // namespace config
}  // namespace vNerve::bilibili