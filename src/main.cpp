#include "bili_session.h"
#include "config.h"

int main(int argc, char** argv)
{
    // TODO main.
    auto opt = vNerve::bilibili::config::parse_options(argc, argv);
    auto session = std::make_shared<vNerve::bilibili::bilibili_session>(opt);
    session->open_connection(21685677);
    while (true);
}