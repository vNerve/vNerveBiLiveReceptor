#include "bilibili_connection_manager.h"
#include "config.h"

#include <spdlog/spdlog.h>

int main(int argc, char** argv)
{
    // TODO main.
    spdlog::set_level(spdlog::level::trace);
    auto opt = vNerve::bilibili::config::parse_options(argc, argv);
    auto session = std::make_shared<vNerve::bilibili::bilibili_connection_manager>(opt);
    session->open_connection(21752681);
    while (true)
        ;
}
