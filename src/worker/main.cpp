#include "bilibili_connection_manager.h"
#include "config.h"

#include <spdlog/spdlog.h>
#include "bilibili_token_updater.h"

int main(int argc, char** argv)
{
    // TODO main.
    spdlog::set_level(spdlog::level::trace);
    auto opt = vNerve::bilibili::config::parse_options(argc, argv);
   // auto session = std::make_shared<vNerve::bilibili::bilibili_connection_manager>(opt, [](int room_id) -> void {  }, [](int room_id, vNerve::bilibili::borrowed_message* msg) -> void {  });
    //session->open_connection(21752681);
    auto token_upd = std::make_shared<vNerve::bilibili::bilibili_token_updater>(opt, [](const std::string& host, int port, const std::string& token) -> void {
        spdlog::info("RECV: {}:{}, {}", host, port, token);
    });
    while (true)
        ;
}
