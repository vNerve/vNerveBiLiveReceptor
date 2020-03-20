#pragma once

#include <boost/program_options.hpp>
#include <memory>

namespace vNerve::bilibili
{
namespace config
{
boost::program_options::options_description create_description();

///
/// Parse options from command line arguments and file.
/// @return Parsed variable. Null when the process should be terminated.
std::shared_ptr<boost::program_options::variables_map> parse_options(
    int argc, char** argv);
}  // namespace config
}  // namespace vNerve::bilibili