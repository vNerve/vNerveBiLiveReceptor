#include "config.h"

namespace vNerve::bilibili::config
{
const std::string DEFAULT_VNERVE_AMQP_SERVER = "localhost";
const int DEFAULT_VNERVE_AMQP_PORT = 5672;
const std::string DEFAULT_VNERVE_AMQP_USER = "guest";
const std::string DEFAULT_VNERVE_AMQP_PASSWORD = "guest";
const std::string DEFAULT_VNERVE_AMQP_VHOST = "/";
const int DEFAULT_VNERVE_AMQP_RECONNECT_SEC = 30;
const std::string DEFAULT_VNERVE_AMQP_EXCHANGE = "vNerve";

const std::string DEFAULT_VNERVE_SERVER = "http://localhost:6161/";
const int DEFAULT_VNERVE_UPDATE_INTERVAL_MINUTES = 30;
const int DEFAULT_VNERVE_UPDATE_TIMEOUT_SEC = 10;

const int DEFAULT_WORKER_PORT = 2434;
const int DEFAULT_WORKER_MQ_THREADS = 1;
const int DEFAULT_WORKER_RECV_TIMEOUT_SEC = 30;
const int DEFAULT_WORKER_CHECK_INTERVAL_MS = 5000;
const int DEFAULT_WORKER_MIN_CHECK_INTERVAL_MS = 2000;
const int DEFAULT_READ_BUFFER = 128 * 1024;
const int DEFAULT_WORKER_INTERVAL_THRESHOLD_SEC = 40;
const int DEFAULT_WORKER_PENALTY_MIN = 5;

const int DEFAULT_MESSAGE_TTL_SEC = 30;

boost::program_options::options_description create_description()
{
    // clang-format off
    using namespace boost::program_options;
    auto descGeneric = options_description("Generic options");
    descGeneric.add_options()
        ("help", "Show help.")
        ("version", "Show version")
    ;

    auto descMQList = options_description("Message Queue broker options");
    descMQList.add_options()
        ("amqp-host", value<std::string>()->default_value(DEFAULT_VNERVE_AMQP_SERVER), "vNerve AMQP Server host.")
        ("amqp-port", value<int>()->default_value(DEFAULT_VNERVE_AMQP_PORT), "vNerve AMQP Server port.")
        ("amqp-user", value<std::string>()->default_value(DEFAULT_VNERVE_AMQP_USER), "vNerve AMQP Server username.")
        ("amqp-password", value<std::string>()->default_value(DEFAULT_VNERVE_AMQP_PASSWORD), "vNerve AMQP Server password.")
        ("amqp-vhost", value<std::string>()->default_value(DEFAULT_VNERVE_AMQP_VHOST), "vNerve AMQP Server vHost.")
        ("amqp-reconnect-interval-sec", value<int>()->default_value(DEFAULT_VNERVE_AMQP_RECONNECT_SEC), "Interval(sec) between reconnecting to AMQP broker.")
        ("amqp-exchange", value<std::string>()->default_value(DEFAULT_VNERVE_AMQP_EXCHANGE), "Exchange name of vNerve AMQP service.")
    ;

    auto descRoomList = options_description("vNerve bilibili info server options");
    descRoomList.add_options()
        ("room-list-update-url", value<std::string>()->default_value(DEFAULT_VNERVE_SERVER), "vNerve VTuber list updating GraphQL url.")
        ("room-list-update-interval,u", value<int>()->default_value(DEFAULT_VNERVE_UPDATE_INTERVAL_MINUTES), "vNerve VTuber list updating interval(min).")
        ("room-list-update-timeout-sec", value<int>()->default_value(DEFAULT_VNERVE_UPDATE_TIMEOUT_SEC), "vNerve VTuber list updating timeout(sec).")
    ;

    auto descWorker = options_description("Worker settings");
    descWorker.add_options()
        ("worker-port", value<int>()->default_value(DEFAULT_WORKER_PORT), "Listening port for workers.")
        ("worker-receive-timeout,t", value<int>()->default_value(DEFAULT_WORKER_RECV_TIMEOUT_SEC), "Timeout for receiving from workers(sec).")
        ("check-interval-ms,c", value<int>()->default_value(DEFAULT_WORKER_CHECK_INTERVAL_MS), "Interval between checking all room/worker state.")
        ("min-check-interval-ms,C", value<int>()->default_value(DEFAULT_WORKER_MIN_CHECK_INTERVAL_MS), "Minimum interval between checking all room/worker state.")
        ("read-buffer,b", value<size_t>()->default_value(DEFAULT_READ_BUFFER), "Reading buffer size(bytes) of sockets to each worker.")
        ("worker-interval-threshold-sec,i", value<int>()->default_value(DEFAULT_WORKER_INTERVAL_THRESHOLD_SEC), "Worker message timeout threshold. Task/Worker which didn't receive any message within this period will fail.")
        ("worker-penalty-min,p", value<int>()->default_value(DEFAULT_WORKER_PENALTY_MIN), "Penalty applied to worker when a task fails. in minutes. No new task will be assign to the worker in the given time period.")
        //("worker-mq-threads, t", value<int>()->default_value(DEFAULT_WORKER_MQ_THREADS), "Thread count for MQ communicating with workers.")
    ;

    auto descMessage = options_description("Message settings");
    descMessage.add_options()
        ("message-ttl-sec,m", value<int>()->default_value(DEFAULT_MESSAGE_TTL_SEC), "Max time to life for a message in deduplicate container.")
    ;

    auto desc = options_description("vNerve Bilibili Livestream chat crawling supervisor");
    desc.add(descGeneric);
    desc.add(descRoomList);
    desc.add(descMQList);
    desc.add(descMessage);
    desc.add(descWorker);
    return desc;
    // clang-format on
}
}  // namespace vNerve::bilibili::config
