#include "config.h"
#include "global_context.h"
#include "config_sv.h"
#include "command_handler_sv.h"
#include "profiler.h"

#include <spdlog/spdlog.h>
#include <spdlog/cfg/env.h>

#ifdef __unix__
#include <stdio.h>
#include <unistd.h>
#endif

using namespace vNerve::bilibili;

int main(int argc, char** argv)
{
    spdlog::set_level(spdlog::level::info);
    spdlog::cfg::load_env_levels();

    auto raw_config = config::parse_options(argc, argv);
    auto config = config::fill_config(raw_config);
    auto config_linker = config::link_config(config);

    auto gc = new supervisor_global_context(config, config_linker);
    profiler::setup_profiling(config.get(), gc);

#ifdef __unix__
    if (!isatty(fileno(stdin)))
    {
        puts("Using non-tty mode.");
        gc->join();
    }
    else
#endif
    try
    {
        while (true)
        {
            std::string input;
            std::getline(std::cin, input);
            command::handle_command_sv(input, config_linker.get());
        }
    }
    catch (command::exit_requested_exception& exited)
    {
        // Just exit
    }
    profiler::teardown_profiling();
    delete gc;
}
