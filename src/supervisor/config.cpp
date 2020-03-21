#include "config.h"

namespace vNerve::bilibili::config
{
const std::string DEFAULT_GRPC_SERVER = "localhost";
const int DEFAULT_GRPC_SERVER_PORT = 6161;
const int DEFAULT_GRPC_UPDATE_INTERVAL_MINUTES = 60;

const std::string DEFAULT_BIND_HOST = "*";
const int DEFAULT_BIND_PORT = 2525;

boost::program_options::options_description create_description()
{
    // clang-format off
    using namespace boost::program_options;
    auto descGeneric = options_description("Generic options");
    descGeneric.add_options()
        ("help", "Show help.")
        ("version", "Show version")
    ;

    auto descGrpc = options_description("vNerve bilibili info gRPC server options");
    descGrpc.add_options()
        ("grpc-server,s", value<std::string>()->default_value(DEFAULT_GRPC_SERVER), "vNerve gRPC server host.")
        ("grpc-server-port,p", value<int>()->default_value(DEFAULT_GRPC_SERVER_PORT), "vNerve gRPC server port.")
        ("grpc-update-interval,u", value<int>()->default_value(DEFAULT_GRPC_UPDATE_INTERVAL_MINUTES), "vNerve VTuber list updating interval(min).")
    ;

    auto descWorker = options_description("Worker settings");
    descWorker.add_options()
        ("port, P", value<int>()->default_value(DEFAULT_BIND_PORT), "Worker MQ binding port.")
        ("host, H", value<std::string>()->default_value(DEFAULT_BIND_HOST), "Worker MQ binding host. Default to '*'")
    ;

    auto desc = options_description("vNerve Bilibili Livestream chat crawling supervisor");
    desc.add(descGeneric);
    desc.add(descGrpc);
    return desc;
    // clang-format on
}
}