#include "config.h"
#include "global_context.h"
#include "windows_minidump.h"

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>
#include "bilibili_live_config.h"
#include <iostream>

#ifdef __unix__
#include <stdio.h>
#include <unistd.h>
#endif

vNerve::bilibili::config::config_t global_config;

int main(int argc, char** argv)
{
    vNerve::bilibili::util::enable_windows_minidump();
    // TODO main.
    spdlog::set_level(spdlog::level::debug);

    spdlog::cfg::load_env_levels();
    global_config = vNerve::bilibili::config::parse_options(argc, argv);
    auto global_ctxt = new vNerve::bilibili::worker_global_context(global_config);
#ifdef __unix__
    if (!isatty(fileno(stdin)))
    {
        puts("Using non-tty mode.");
        global_ctxt->join();
    }
    else
#endif
    while (true)
    {
        std::string command;
        std::cin >> command;
        spdlog::set_level(spdlog::level::from_str(command));
    }
    delete global_ctxt;
}
