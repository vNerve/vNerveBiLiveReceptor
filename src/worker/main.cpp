#include "config.h"
#include "global_context.h"
#include "windows_minidump.h"

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

vNerve::bilibili::config::config_t global_config;

int main(int argc, char** argv)
{
    vNerve::bilibili::util::enable_windows_minidump();
    // TODO main.
    spdlog::set_level(spdlog::level::info);
    spdlog::cfg::load_env_levels();
    global_config = vNerve::bilibili::config::parse_options(argc, argv);
    auto global_ctxt = new vNerve::bilibili::worker_global_context(global_config);
    global_ctxt->join();
    delete global_ctxt;
}
