#include "config.h"

#include "version.h"

#include <iostream>

namespace vNerve::bilibili::config
{
std::shared_ptr<boost::program_options::variables_map> parse_options(
    int argc, char** argv)
{
    using namespace boost::program_options;
    auto desc = create_description();
    std::shared_ptr<variables_map> result = std::make_shared<variables_map>();
    store(parse_command_line(argc, argv, desc), *result);

    if (result->count("help"))
    {
        std::cerr << desc << std::endl;
        return std::shared_ptr<variables_map>();
    }

    if (result->count("version"))
    {
        std::cerr << VERSION << std::endl;
        return std::shared_ptr<variables_map>();
    }
    return result;
}
}  // namespace vNerve::bilibili::config
