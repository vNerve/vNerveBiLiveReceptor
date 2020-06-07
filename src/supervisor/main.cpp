#include "room_list_updater.h"

#include "config.h"
#include "amqp_client.h"

#include <memory>
#include <iostream>
#include <chrono>
#include <thread>
#include <spdlog/spdlog.h>

int main(int argc, char** argv)
{
    auto opt = vNerve::bilibili::config::parse_options(argc, argv);
    auto amqpctxt = new vNerve::bilibili::mq::amqp_context(opt);
    spdlog::set_level(spdlog::level::trace);
    while (true)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::string str = "avdsasbdsfb";
        amqpctxt->post_payload("test", reinterpret_cast<unsigned char const*>(str.c_str()), str.size());
    }
}
