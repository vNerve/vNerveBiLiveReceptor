#include "config.h"

namespace vNerve::bilibili::config
{
// Default options.
const int DEFAULT_HEARTBEAT_TIMEOUT_SEC = 25;
const std::string DEFAULT_CHAT_SERVER = "broadcastlv.chat.bilibili.com";
const std::string DEFAULT_CHAT_SERVER_CONFIG_URL = "https://api.live.bilibili.com/room/v1/Danmu/getConf";
const std::string DEFAULT_CHAT_SERVER_CONFIG_USER_AGENT = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/83.0.4103.97 Safari/537.36";
const std::string DEFAULT_CHAT_SERVER_CONFIG_REFERER = "https://live.bilibili.com/";
const int DEFAULT_CHAT_SERVER_CONFIG_INTERVAL_SEC = 5;
const int DEFAULT_CHAT_SERVER_CONFIG_TIMEOUT_SEC = 15;
const int DEFAULT_CHAT_SERVER_PORT = 2243;
const int DEFAULT_CHAT_SERVER_PROTOCOL_VER = 2;

const int DEFAULT_READ_BUFFER = 128 * 1024;
const int DEFAULT_THREADS = 1;

const std::string DEFAULT_SUPERVISOR_HOST = "localhost";
const int DEFAULT_SUPERVISOR_PORT = 2434; // see also supervisor/config.cpp
const int DEFAULT_MAX_ROOMS = 500;
const int DEFAULT_MAX_RETRY_SEC = 60;
const std::string DEFAULT_AUTH_CODE = "abcdefghijklmnopqrstuvwyzabcdef"; // see also supervisor/config.cpp

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
        ("chat-config-url", value<std::string>()->default_value(DEFAULT_CHAT_SERVER_CONFIG_URL),"Bilibili live chat config URL.")
        ("chat-config-user-agent", value<std::string>()->default_value(DEFAULT_CHAT_SERVER_CONFIG_USER_AGENT),"User-Agent used in requesting bilibili live chat config URL.")
        ("chat-config-referer", value<std::string>()->default_value(DEFAULT_CHAT_SERVER_CONFIG_REFERER),"Referer used in requesting bilibili chat config URL.")
        ("chat-config-interval-sec", value<int>()->default_value(DEFAULT_CHAT_SERVER_CONFIG_INTERVAL_SEC),"Interval between requesting bilibili chat config URL.")
        ("chat-config-timeout-sec", value<int>()->default_value(DEFAULT_CHAT_SERVER_CONFIG_TIMEOUT_SEC),"Timeout requesting bilibili chat config URL.")
    ;

    auto descSupervisor = options_description("vNerve bilibili chat supervisor options");
    descSupervisor.add_options()
        ("supervisor-host,H", value<std::string>()->default_value(DEFAULT_SUPERVISOR_HOST), "vNerve Bilibili chat supervisor port. Default to localhost")
        ("supervisor-port,P", value<int>()->default_value(DEFAULT_SUPERVISOR_PORT), "vNerve Bilibili chat supervisor host. Default to 2434")
        ("max-rooms,M", value<int>()->default_value(DEFAULT_MAX_ROOMS), "Max concurrent connecting rooms.")
        ("retry-interval-sec,R", value<int>()->default_value(DEFAULT_MAX_RETRY_SEC), "Interval between retrying to connect to supervisor. In seconds.")
        ("auth-code,A", value<std::string>()->default_value(DEFAULT_AUTH_CODE), "Auth code for authentication.")
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
