#pragma once

#include <boost/program_options.hpp>
#include <memory>

namespace vNerve::bilibili
{
namespace config
{
using config_t = std::shared_ptr<boost::program_options::variables_map>;

boost::program_options::options_description create_description();

///
/// Parse options from command line arguments and file.
/// @return Parsed variable. Null when the process should be terminated.
config_t parse_options(int argc, char** argv);
}  // namespace config
}  // namespace vNerve::bilibili