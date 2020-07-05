#include "profiler.h"

#include <boost/algorithm/string.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <mutex>

namespace vNerve::bilibili::profiler
{
Remotery* global_rmt = nullptr;
template <typename Mutex>
class remotery_sink : public spdlog::sinks::base_sink<Mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        auto str = fmt::to_string(formatted);
        boost::trim_right(str);
        rmt_LogText(str.c_str());
    }

    void flush_() override
    {
    }
};

using remotery_sink_mt = remotery_sink<std::mutex>;

void handle_remotery_input(const char* text, void* context)
{
    reinterpret_cast<supervisor_global_context*>(context)->handle_command(std::string_view(text));
}

void setup_profiling(config::config_supervisor* config, supervisor_global_context* ctxt)
{
    if (global_rmt)
        rmt_DestroyGlobalInstance(global_rmt);
    rmtSettings* settings = rmt_Settings();
    settings->port = config->diag.profiler_port;
    settings->limit_connections_to_localhost = config->diag.profiler_limit_localhost;
    settings->input_handler_context = ctxt;
    settings->input_handler = handle_remotery_input;

    rmt_CreateGlobalInstance(&global_rmt);
    spdlog::default_logger()->sinks().push_back(std::make_shared<remotery_sink_mt>());
}

void teardown_profiling()
{
    rmt_DestroyGlobalInstance(global_rmt);
    global_rmt = nullptr;
}
}
