#include "room_list_updater.h"
#include "version.h"

#ifdef _WIN32
// ReSharper disable CppUnusedIncludeDirective
#include <Windows.h> // for httplib.h
// ReSharper restore CppUnusedIncludeDirective
#endif

#include "rapidjson/document.h"

namespace vNerve::bilibili::info
{
vtuber_info_updater::vtuber_info_updater(
    std::shared_ptr<boost::program_options::variables_map> options,
    vtuber_info_update_callback callback)
    : _guard(_context.get_executor()),
      _timer(std::make_unique<boost::asio::deadline_timer>(_context)),
      _update_interval_min((*options)["room-list-update-interval"].as<int>()),
      _server_endpoint((*options)["room-list-endpoint"].as<std::string>()),
      _http_client(
          (*options)["room-list-host"].as<std::string>(),
          (*options)["room-list-port"].as<int>()),
      _options(options),
      _callback(callback)
{
    _thread =
        boost::thread(boost::bind(&boost::asio::io_context::run, &_context));

    _timer->expires_from_now(boost::posix_time::seconds(1));
    _timer->async_wait(
        boost::bind(&vtuber_info_updater::on_timer_tick, this,
                    boost::asio::placeholders::error));

    _http_headers.emplace("User-Agent", std::string("vNerve ") + VERSION);
}

vtuber_info_updater::~vtuber_info_updater()
{
    _timer->cancel();
    _guard.reset();
    _context.stop();
    // TODO exception handling
}

void vtuber_info_updater::reschedule_timer()
{
    _timer->expires_from_now(boost::posix_time::minutes(_update_interval_min));
    _timer->async_wait(boost::bind(&vtuber_info_updater::on_timer_tick, this,
                                   boost::asio::placeholders::error));
}

const std::string bilibili_info_request_body = R"EOF(
{"query":"{allBilibiliInfo{liveInfo{roomId}}}"}
)EOF";
void vtuber_info_updater::refresh()
{
    auto response =
        _http_client.Post(_server_endpoint.c_str(), _http_headers,
                          bilibili_info_request_body, "application/json");
    if (!response)
    {
        // TODO logging
        return;
    }
    if (response->status != 200)
    {
        // TODO logging
        return;
    }
    using namespace rapidjson;
    auto body = response->body;

    Document document;
    ParseResult result = document.Parse(body.c_str());
    if (result.IsError())
    {
        // TODO logging
        return;
    }
    if (!document.IsObject())
    {
        return;
    }
    auto data_iter = document.FindMember("data");
    if (data_iter == document.MemberEnd() || !data_iter->value.IsObject())
    {
        // todo logging
        return;
    }
    auto all_bilibili_info_iter =
        data_iter->value.GetObjectA().FindMember("allBilibiliInfo");
    if (all_bilibili_info_iter == data_iter->value.MemberEnd() || !all_bilibili_info_iter->value.IsArray())
    {
        // todo logging
        return;
    }

    auto room_ids = std::vector<int>();
    for (auto& element : all_bilibili_info_iter->value.GetArray())
    {
        if (!element.IsObject())
            continue;
        auto obj = element.GetObjectA();
        auto live_info_iter = obj.FindMember("liveInfo");
        if (live_info_iter == obj.MemberEnd() || !live_info_iter->value.IsObject())
            continue;
        auto live_info_obj = live_info_iter->value.GetObjectA();
        auto room_id_iter = live_info_obj.FindMember("roomId");
        if (room_id_iter == obj.MemberEnd() || !room_id_iter->value.IsInt())
            continue;
        auto room_id = room_id_iter->value.GetInt();
        room_ids.push_back(room_id);
    }

    _callback(room_ids);
}

void vtuber_info_updater::on_timer_tick(const boost::system::error_code& err)
{
    if (err)
    {
        if (err.value() == boost::asio::error::operation_aborted)
        {
            return; // closing socket.
        }
        // TODO logging
    }

    refresh();
    reschedule_timer();
}
}
