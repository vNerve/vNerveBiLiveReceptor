#include "config.h"
#include "global_context.h"
#include "windows_minidump.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/cfg/env.h>
#include "bilibili_live_config.h"
#include <iostream>

#ifdef __unix__
#include <stdio.h>
#include <unistd.h>
#endif
#ifdef WIN32
#include <io.h>
#endif

vNerve::bilibili::config::config_t global_config;

int main(int argc, char** argv)
{
    vNerve::bilibili::util::enable_windows_minidump();
    spdlog::set_level(spdlog::level::info);
#ifdef WIN32
    if (!_isatty(_fileno(stdout)))
    {
        auto max_size = 1048576 * 16;
        auto max_files = 32;
        auto& sinks = spdlog::default_logger()->sinks();
        sinks.emplace_back(new spdlog::sinks::rotating_file_sink_mt("logs/vNerveBiLiveWorker.log", max_size, max_files));
    }
#endif

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
#ifdef WIN32
    if (!_isatty(_fileno(stdout)))
    {
        puts("Using non-tty mode.");
        global_ctxt->join();
    }
#endif
    while (true)
    {
        std::string command;
        std::cin >> command;
        spdlog::set_level(spdlog::level::from_str(command));
    }
    delete global_ctxt;
}
