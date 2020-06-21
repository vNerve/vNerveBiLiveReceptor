#include "config.h"
#include "global_context.h"

#include <spdlog/spdlog.h>

int main(int argc, char** argv)
{
    auto opt = vNerve::bilibili::config::parse_options(argc, argv);
    spdlog::set_level(spdlog::level::info);
    auto gc = new vNerve::bilibili::supervisor_global_context(opt);
    gc->join();
    delete gc;
}
