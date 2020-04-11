#include "room_list_updater.h"

#include "config.h"

#include <memory>
#include <iostream>

int main(int argc, char** argv)
{
    auto opt = vNerve::bilibili::config::parse_options(argc, argv);
    auto updater =
        std::make_shared<vNerve::bilibili::info::vtuber_info_updater>(opt, [](auto& vec) -> void
    {
                for (int x : vec)
                {
                    std::cout << x << std::endl;
                }
                std::cout << "Total: " << vec.size() << std::endl;
    });
    while (1)
        ;
}
