#include "config.h"
#include "version.h"

#include <iostream>

namespace vNerve::bilibili::config
{
boost::program_options::options_description create_description()
{
    using namespace boost::program_options;
    auto desc = options_description("vNerve Bilibili Livestream chat crawling supervisor");
    return desc;
}
}