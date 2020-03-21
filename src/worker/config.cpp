#include "config.h"

namespace vNerve::bilibili::config
{
// Default options.
const int DEFAULT_HEARTBEAT_TIMEOUT_SEC = 40;
const std::string DEFAULT_CHAT_SERVER = "broadcastlv.chat.bilibili.com";
const int DEFAULT_CHAT_SERVER_PORT = 2243;
const int DEFAULT_CHAT_SERVER_PROTOCOL_VER = 2;

const int DEFAULT_READ_BUFFER = 128 * 1024;
const int DEFAULT_THREADS = 1;

const std::string DEFAULT_SUPERVISOR_HOST = "localhost";
const int DEFAULT_SUPERVISOR_PORT = 2525;

boost::program_options::options_description create_description()
{
    // clang-format off
    using namespace boost::program_options;
    auto descGeneric = options_description("Generic options");
    descGeneric.add_options()
        ("help", "Show help.")
        ("version", "Show version");

    auto descNetworking = options_description("Networking parameters");
    descNetworking.add_options()
        ("read-buffer,b", value<size_t>()->default_value(DEFAULT_READ_BUFFER), "Reading buffer size(bytes) of sockets to bilibili server.")
        ("zlib-buffer", value<size_t>()->default_value(DEFAULT_READ_BUFFER), "Reading buffer size(bytes) for storing unzipped bilibili chat packet.")
        ("threads", value<int>()->default_value(DEFAULT_THREADS), "Thread numbers for communicating with bilibili server.")
    ;

    auto descBili = options_description("Bilibili Livestream Interface options");
    descBili.add_options()
        ("heartbeat-timeout,t", value<int>()->default_value(DEFAULT_HEARTBEAT_TIMEOUT_SEC), "Timeout(secs) between heartbeat packets to Bilibili server.")
        ("chat-server,s", value<std::string>()->default_value(DEFAULT_CHAT_SERVER), "Bilibili live chat server in TCP mode.")
        ("chat-server-port,p", value<int>()->default_value(DEFAULT_CHAT_SERVER_PORT), "Bilibili live chat server port.")
        ("protocol-ver,V", value<int>()->default_value(DEFAULT_CHAT_SERVER_PROTOCOL_VER),"Bilibili live chat server protocol version.")
    ;

    auto descSupervisor = options_description("vNerve bilibili chat supervisor options");
    descSupervisor.add_options()
        ("supervisor-host, S", value<std::string>()->default_value(DEFAULT_SUPERVISOR_HOST), "vNerve Bilibili chat supervisor host")
        ("supervisor-port, P", value<int>()->default_value(DEFAULT_SUPERVISOR_PORT), "vNerve Bilibili chat supervisor port")
    ;

    auto desc = options_description("vNerve Bilibili Livestream chat crawling worker");
    desc.add(descGeneric);
    desc.add(descNetworking);
    desc.add(descBili);
    desc.add(descSupervisor);

    return desc;
    // clang-format on
}
}  // namespace vNerve::bilibili::config
