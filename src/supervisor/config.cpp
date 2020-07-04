#include "config.h"
#include "config_sv.h"

namespace vNerve::bilibili::config
{
const std::string DEFAULT_VNERVE_AMQP_SERVER = "localhost";
const int DEFAULT_VNERVE_AMQP_PORT = 5672;
const std::string DEFAULT_VNERVE_AMQP_USER = "guest";
const std::string DEFAULT_VNERVE_AMQP_PASSWORD = "guest";
const std::string DEFAULT_VNERVE_AMQP_VHOST = "/";
const int DEFAULT_VNERVE_AMQP_RECONNECT_SEC = 30;
const std::string DEFAULT_VNERVE_AMQP_EXCHANGE = "vNerve";
const std::string DEFAULT_VNERVE_AMQP_DIAG_EXCHANGE = "vNerveDiag";

const std::string DEFAULT_VNERVE_SERVER = "http://localhost:6161/";
const int DEFAULT_VNERVE_UPDATE_INTERVAL_MINUTES = 30;
const int DEFAULT_VNERVE_UPDATE_TIMEOUT_SEC = 10;

const int DEFAULT_WORKER_PORT = 2434; // see also worker/config.cpp
const int DEFAULT_WORKER_MQ_THREADS = 1;
const int DEFAULT_WORKER_RECV_TIMEOUT_SEC = 45;
const int DEFAULT_WORKER_CHECK_INTERVAL_MS = 5000;
const int DEFAULT_WORKER_MIN_CHECK_INTERVAL_MS = 2000;
const int DEFAULT_WORKER_MAX_NEW_TASKS_PER_BUNCH = 4;
const int DEFAULT_READ_BUFFER = 128 * 1024;
const int DEFAULT_WORKER_INTERVAL_THRESHOLD_SEC = 40;
const int DEFAULT_WORKER_PENALTY_MIN = 1;
const std::string DEFAULT_AUTH_CODE = "abcdefghijklmnopqrstuvwyzabcdef";

const int DEFAULT_MESSAGE_TTL_SEC = 30;
const int DEFAULT_MIN_INTERVAL_POPULARITY_SEC = 20;

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
        ("amqp-diag-exchange", value<std::string>()->default_value(DEFAULT_VNERVE_AMQP_DIAG_EXCHANGE), "Exchange name of vNerve AMQP Diagnostics service.")
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
        ("check-interval-ms,c", value<int>()->default_value(DEFAULT_WORKER_CHECK_INTERVAL_MS), "Interval between checking all room/worker state.")
        ("min-check-interval-ms,C", value<int>()->default_value(DEFAULT_WORKER_MIN_CHECK_INTERVAL_MS), "Minimum interval between checking all room/worker state.")
        ("read-buffer,b", value<size_t>()->default_value(DEFAULT_READ_BUFFER), "Reading buffer size(bytes) of sockets to each worker.")
        ("worker-interval-threshold-sec,i", value<int>()->default_value(DEFAULT_WORKER_INTERVAL_THRESHOLD_SEC), "Worker message timeout threshold. Task/Worker which didn't receive any message within this period will fail.")
        ("worker-penalty-min,p", value<int>()->default_value(DEFAULT_WORKER_PENALTY_MIN), "Penalty applied to worker when a task fails. in minutes. No new task will be assign to the worker in the given time period.")
        ("worker-max-new-tasks-per-bunch,M", value<int>()->default_value(DEFAULT_WORKER_MAX_NEW_TASKS_PER_BUNCH), "Max new task assigned to a single worker every bunch.")
        ("auth-code,A", value<std::string>()->default_value(DEFAULT_AUTH_CODE), "Auth code for worker.")
        //("worker-mq-threads, t", value<int>()->default_value(DEFAULT_WORKER_MQ_THREADS), "Thread count for MQ communicating with workers.")
    ;

    auto descMessage = options_description("Message settings");
    descMessage.add_options()
        ("message-ttl-sec,m", value<int>()->default_value(DEFAULT_MESSAGE_TTL_SEC), "Max time to life for a message in deduplicate container.")
        ("min-interval-popularity-sec", value<int>()->default_value(DEFAULT_MIN_INTERVAL_POPULARITY_SEC), "Max time between two popularity update packets.")
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

std::shared_ptr<config_supervisor> fill_config(config::config_t raw)
{
    auto& rawr = *raw;
    auto result = std::make_shared<config_supervisor>();

    auto& amqp = result->amqp;
    amqp.host = rawr["amqp-host"].as<std::string>();
    amqp.port = rawr["amqp-port"].as<int>();
    amqp.user = rawr["amqp-user"].as<std::string>();
    amqp.password = rawr["amqp-password"].as<std::string>();
    amqp.vhost = rawr["amqp-vhost"].as<std::string>();
    amqp.reconnect_interval_sec = rawr["amqp-reconnect-interval-sec"].as<int>();
    amqp.exchange = rawr["amqp-exchange"].as<std::string>();
    amqp.diag_exchange = rawr["amqp-diag-exchange"].as<std::string>();

    auto& rlu = result->room_list_updater;
    rlu.url = rawr["room-list-update-url"].as<std::string>();
    rlu.interval_min = rawr["room-list-update-interval"].as<int>();
    rlu.timeout_sec = rawr["room-list-update-timeout-sec"].as<int>();

    auto& worker = result->worker;
    worker.port = rawr["worker-port"].as<int>();
    worker.auth_code = rawr["auth-code"].as<std::string>();
    worker.worker_timeout_sec = rawr["worker-interval-threshold-sec"].as<int>();
    worker.check_interval_msec = rawr["check-interval-ms"].as<int>();
    worker.min_check_interval_msec = rawr["min-check-interval-ms"].as<int>();
    worker.worker_penalty_min = rawr["worker-penalty-min"].as<int>();
    worker.read_buffer_size = rawr["read-buffer"].as<size_t>();
    worker.max_new_tasks_per_bunch = rawr["worker-max-new-tasks-per-bunch"].as<int>();

    auto& message = result->message;
    message.message_ttl_sec = rawr["message-ttl-sec"].as<int>();
    message.min_interval_popularity_sec = rawr["min-interval-popularity-sec"].as<int>();

    return result;
}

std::shared_ptr<config_dynamic_linker> link_config(config_sv_t config)
{
    auto result = std::make_shared<config_dynamic_linker>();

    result->register_entry("amqp-host", static_cast<std::string*>(nullptr), false);
    result->register_entry("amqp-port", static_cast<int*>(nullptr), false);
    result->register_entry("amqp-user", static_cast<std::string*>(nullptr), false);
    result->register_entry("amqp-password", static_cast<std::string*>(nullptr), false);
    result->register_entry("amqp-vhost", static_cast<std::string*>(nullptr), false);
    result->register_entry("amqp-reconnect-interval-sec", static_cast<int*>(nullptr), false);
    result->register_entry("amqp-exchange", static_cast<std::string*>(nullptr), false);
    result->register_entry("amqp-diag-exchange", static_cast<std::string*>(nullptr), false);

    result->register_entry("room-list-update-url", &config->room_list_updater.url, true);
    result->register_entry("room-list-update-interval", &config->room_list_updater.interval_min, true);
    result->register_entry("room-list-update-timeout-sec", &config->room_list_updater.timeout_sec, true);

    result->register_entry("worker-port", static_cast<int*>(nullptr), false);
    result->register_entry("auth-code", &config->worker.auth_code, true);
    result->register_entry("worker-interval-threshold-sec", &config->worker.worker_timeout_sec, true);
    result->register_entry("check-interval-ms", &config->worker.check_interval_msec, true);
    result->register_entry("min-check-interval-ms", &config->worker.min_check_interval_msec, true);
    result->register_entry("worker-penalty-min", &config->worker.worker_penalty_min, true);
    result->register_entry("read-buffer", static_cast<int*>(nullptr), false);
    result->register_entry("worker-max-new-tasks-per-bunch", &config->worker.max_new_tasks_per_bunch, true);

    result->register_entry("message-ttl-sec", &config->message.message_ttl_sec, true);
    result->register_entry("min-interval-popularity-sec", &config->message.min_interval_popularity_sec, true);

    return result;
}

}  // namespace vNerve::bilibili::config
