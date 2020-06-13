#include "room_list_updater.h"
#include "version.h"

#include <rapidjson/document.h>
#include <utility>
#include <spdlog/spdlog.h>

namespace vNerve::bilibili::info
{
vtuber_info_updater::vtuber_info_updater(
    std::shared_ptr<boost::program_options::variables_map> options,
    vtuber_info_update_callback callback)
    : http_interval_updater((*options)["room-list-update-interval"].as<int>(), (*options)["room-list-update-timeout-sec"].as<int>()),
      _server_url((*options)["room-list-update-url"].as<std::string>()),
      _options(options),
      _callback(std::move(callback))
{
    _user_agent = std::string("vNerve Bilibili Live Receptor") + VERSION;
}

const std::string bilibili_info_request_body = R"EOF(
{"query":"{allBilibiliInfo{liveInfo{roomId}}}"}
)EOF";

const char* vtuber_info_updater::on_request_method() { return "POST"; }
const char* vtuber_info_updater::on_request_payload() { return bilibili_info_request_body.c_str(); }
const char* vtuber_info_updater::on_request_content_type() { return "application/json"; }
const char* vtuber_info_updater::on_request_accept()  { return "application/json"; }
const char* vtuber_info_updater::on_request_referer() { return nullptr; }

void vtuber_info_updater::on_updated(std::string_view body)
{
    using namespace rapidjson;

    Document document;
    ParseResult result = document.Parse(body.data());
    if (result.IsError())
    {
        spdlog::warn("[room_list_upd] Invalid vNerve GraphQL Response: Failed to parse JSON:{}", result.Code());
        return;
    }
    if (!document.IsObject())
    {
        spdlog::warn("[room_list_upd] Invalid vNerve GraphQL Response: Root element is not JSON object.");
        return;
    }
    auto data_iter = document.FindMember("data");
    if (data_iter == document.MemberEnd() || !data_iter->value.IsObject())
    {
        spdlog::warn("[room_list_upd] Invalid vNerve GraphQL Response: no data object.");
        return;
    }
    auto all_bilibili_info_iter =
        data_iter->value.GetObjectA().FindMember("allBilibiliInfo");
    if (all_bilibili_info_iter == data_iter->value.MemberEnd() || !all_bilibili_info_iter->value.IsArray())
    {
        spdlog::warn("[room_list_upd] Malformed vNerve GraphQL Response: allBilibiliInfo isn't array.");
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

    SPDLOG_DEBUG("[room_list_upd] Received new VTuber room list, size={}", room_ids.size());

    _callback(room_ids);
}
}
