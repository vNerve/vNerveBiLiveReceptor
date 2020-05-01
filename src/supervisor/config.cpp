#include "config.h"

namespace vNerve::bilibili::config
{
const std::string DEFAULT_VNERVE_SERVER = "localhost";
const std::string DEFAULT_VNERVE_ENDPOINT = "/";
const int DEFAULT_VNERVE_SERVER_PORT = 6161;
const int DEFAULT_VNERVE_UPDATE_INTERVAL_MINUTES = 30;
const bool DEFAULT_VNERVE_HTTPS = false;

const std::string DEFAULT_SUPERVISOR_ENDPOINT = "tcp://*:2525";
const int DEFAULT_WORKER_MQ_THREADS = 1;
const int DEFAULT_WORKER_RECV_TIMEOUT_SEC = 30;
const int DEFAULT_WORKER_CHECK_INTERVAL_MS = 5000;
const int DEFAULT_WORKER_MIN_CHECK_INTERVAL_MS = 2000;
const int DEFAULT_READ_BUFFER = 128 * 1024;
const int DEFAULT_WORKER_INTERVAL_THRESHOLD_SEC = 40;

boost::program_options::options_description create_description()
{
    // clang-format off
    using namespace boost::program_options;
    auto descGeneric = options_description("Generic options");
    descGeneric.add_options()
        ("help", "Show help.")
        ("version", "Show version")
    ;

    auto descRoomList = options_description("vNerve bilibili info server options");
    descRoomList.add_options()
        ("room-list-host", value<std::string>()->default_value(DEFAULT_VNERVE_SERVER), "vNerve server host.")
        ("room-list-port", value<int>()->default_value(DEFAULT_VNERVE_SERVER_PORT), "vNerve server port.")
        ("room-list-endpoint", value<std::string>()->default_value(DEFAULT_VNERVE_ENDPOINT), "vNerve server endpoint. start with '/'")
        ("room-list-update-interval,u", value<int>()->default_value(DEFAULT_VNERVE_UPDATE_INTERVAL_MINUTES), "vNerve VTuber list updating interval(min).")
    ;

    auto descWorker = options_description("Worker settings");
    descWorker.add_options()
        ("supervisor-endpoint, e", value<std::string>()->default_value(DEFAULT_SUPERVISOR_ENDPOINT), "vNerve Bilibili chat supervisor binding address. Default to tcp://*:2525")
        ("worker-receive-timeout,t", value<int>()->default_value(DEFAULT_WORKER_RECV_TIMEOUT_SEC), "Timeout for receiving from workers(sec).")
        ("check-interval-ms,c", value<int>()->default_value(DEFAULT_WORKER_CHECK_INTERVAL_MS), "Interval between checking all room/worker state.")
        ("min-check-interval-ms,C", value<int>()->default_value(DEFAULT_WORKER_MIN_CHECK_INTERVAL_MS), "Minimum interval between checking all room/worker state.")
        ("read-buffer,b", value<size_t>()->default_value(DEFAULT_READ_BUFFER), "Reading buffer size(bytes) of sockets to each worker.")
        ("worker-interval-threshold-sec,i", value<int>()->default_value(DEFAULT_WORKER_INTERVAL_THRESHOLD_SEC), "Worker message timeout threshold. Task/Worker which didn't receive any message within this period will fail.")
        //("worker-mq-threads, t", value<int>()->default_value(DEFAULT_WORKER_MQ_THREADS), "Thread count for MQ communicating with workers.")
    ;

    auto desc = options_description("vNerve Bilibili Livestream chat crawling supervisor");
    desc.add(descGeneric);
    desc.add(descRoomList);
    desc.add(descWorker);
    return desc;
    // clang-format on
}
}  // namespace vNerve::bilibili::config
