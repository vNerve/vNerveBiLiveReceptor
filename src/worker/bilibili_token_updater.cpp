#include "bilibili_token_updater.h"

#include <rapidjson/document.h>
#include <utility>
#include <spdlog/spdlog.h>

namespace vNerve::bilibili
{
const char* bilibili_token_updater::on_request_method() { return "GET"; }
const char* bilibili_token_updater::on_request_accept() { return "application/json"; }

void bilibili_token_updater::on_updated(std::string_view body)
{
    using namespace rapidjson;

    Document document;
    ParseResult result = document.Parse(body.data());
    if (result.IsError())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Failed to parse JSON:{}", result.Code());
        return;
    }
    if (!document.IsObject())
    {
        spdlog::warn("[bili_token_upd] IInvalid Bilibili Live Chat Config Response: Root element is not JSON object.");
        return;
    }
    auto code_iter = document.FindMember("code");
    if (code_iter == document.MemberEnd() || !code_iter->value.IsInt())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: no response code.");
        return;
    }
    if (code_iter->value.GetInt() != 0)
    {
        auto msg_iter = document.FindMember("msg");
        auto message_iter = document.FindMember("message");
        if (msg_iter == document.MemberEnd() || !msg_iter->value.IsString() || message_iter == document.MemberEnd() || !message_iter->value.IsString())
            spdlog::warn("[bili_token_upd] Error in Bilibili Live Chat Config Response: code={}", code_iter->value.GetInt());
        else
            spdlog::warn("[bili_token_upd] Error in Bilibili Live Chat Config Response: code={}, msg={}, message={}",
                         code_iter->value.GetInt(), msg_iter->value.GetString(), message_iter->value.GetString());
        return;
    }
    auto data_iter = document.FindMember("data");
    if (data_iter == document.MemberEnd() || !data_iter->value.IsObject())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: no data object.");
        return;
    }

    auto token_iter = data_iter->value.FindMember("token");
    if (token_iter == data_iter->value.MemberEnd() || !token_iter->value.IsString())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Token does not exist or isn't string.");
        return;
    }

    auto server_list_iter = data_iter->value.FindMember("host_server_list");
    if (server_list_iter == data_iter->value.MemberEnd() || !server_list_iter->value.IsArray())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Server list doesn't exist or isn't array.");
        return;
    }
    if (server_list_iter->value.GetArray().Size() <= 0)
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: No chat server provided.");
        return;
    }

    auto chat_server = server_list_iter->value.GetArray().Begin();
    if (!chat_server->IsObject())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Chat server info isn't JSON object.");
        return;
    }

    auto char_server_host_iter = chat_server->FindMember("host");
    auto char_server_port_iter = chat_server->FindMember("port");
    if (char_server_host_iter == chat_server->MemberEnd() || !char_server_host_iter->value.IsString()
        || char_server_port_iter == chat_server->MemberEnd() || !char_server_port_iter->value.IsInt())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: port or host doesn't exist or valid.");
        return;
    }

    auto token = std::string(token_iter->value.GetString(), token_iter->value.GetStringLength());
    auto host = std::string(char_server_host_iter->value.GetString(), char_server_host_iter->value.GetStringLength());
    auto port = char_server_port_iter->value.GetInt();
    SPDLOG_DEBUG("[bili_token_upd] Received new bilibili live chat config. token={}, host={}, port={}", token, host, port);

    _callback(host, port, token);
}

bilibili_token_updater::bilibili_token_updater(const config::config_t config, bilibili_token_updater_callback callback)
    : http_interval_updater((*config)["chat-config-interval-min"].as<int>(), (*config)["chat-config-timeout-sec"].as<int>()),
      _url((*config)["chat-config-url"].as<std::string>()),
      _user_agent((*config)["chat-config-user-agent"].as<std::string>()),
      _referer((*config)["chat-config-referer"].as<std::string>()),
      _callback(std::move(callback))
{
}
}
