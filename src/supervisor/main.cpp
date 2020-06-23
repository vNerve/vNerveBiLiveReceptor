#include "config.h"
#include "global_context.h"
#include "windows_minidump.h"

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

int main(int argc, char** argv)
{
    spdlog::cfg::load_env_levels();
    auto opt = vNerve::bilibili::config::parse_options(argc, argv);
    spdlog::set_level(spdlog::level::info);
    auto gc = new vNerve::bilibili::supervisor_global_context(opt);
    gc->join();
    delete gc;
}
