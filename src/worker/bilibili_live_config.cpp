#include "bilibili_live_config.h"

#include <functional>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <uriparser/Uri.h>
#include <spdlog/spdlog.h>
#include <rapidjson/document.h>

namespace vNerve::bilibili
{
using namespace boost::asio;
using namespace boost::beast;
using boost::system::error_code;

namespace live_config
{
ssl::context* ssl_context = nullptr;

bool configure_ssl_context()
{
    ssl_context = new ssl::context{ssl::context::tls_client};
    try
    {
        ssl_context->load_verify_file("cacert.pem");
    }
    catch (boost::system::system_error& err)
    {
        spdlog::critical("[live_cfg] Failed to initialize ssl_context from file cacert.pem! Ensure you have a valid CA cert file. err:{}:{}", err.code().value(), err.code().message());
        throw;
    }
    spdlog::info("[live_cfg] Initialized SSL Context from cacert.pem.");
    return true;
}
static const bool ssl_context_configured = configure_ssl_context();
}
using namespace live_config;

struct bilibili_live_config_fetcher
{
    void operator()(boost::iterator_range<char const *> const &, error_code const &)
    {
    }
};

std::string path_seg_to_string(UriPathSegmentA* xs, const std::string& delim)
{
    UriPathSegmentStructA* head(xs);
    std::string ret;

    while (head)
    {
        ret += delim + std::string(head->text.first, head->text.afterLast);
        head = head->next;
    }

    return ret;
}

void parse_bilibili_config(std::string const& buf, std::function<void(bilibili_live_config const&)> const& on_success, std::function<void()> const& on_failed)
{
    using namespace rapidjson;

    Document document;
    ParseResult result = document.Parse(buf.c_str());
    if (result.IsError())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Failed to parse JSON:{}", result.Code());
        return on_failed();
    }
    if (!document.IsObject())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Root element is not JSON object.");
        return on_failed();
    }
    auto code_iter = document.FindMember("code");
    if (code_iter == document.MemberEnd() || !code_iter->value.IsInt())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: no response code.");
        return on_failed();
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
        return on_failed();
    }
    auto data_iter = document.FindMember("data");
    if (data_iter == document.MemberEnd() || !data_iter->value.IsObject())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: no data object.");
        return on_failed();
    }

    auto token_iter = data_iter->value.FindMember("token");
    if (token_iter == data_iter->value.MemberEnd() || !token_iter->value.IsString())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Token does not exist or isn't string.");
        return on_failed();
    }

    auto server_list_iter = data_iter->value.FindMember("host_server_list");
    if (server_list_iter == data_iter->value.MemberEnd() || !server_list_iter->value.IsArray())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Server list doesn't exist or isn't array.");
        return on_failed();
    }
    if (server_list_iter->value.GetArray().Size() <= 0)
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: No chat server provided.");
        return on_failed();
    }

    auto chat_server = server_list_iter->value.Begin();
    auto succ = false;
    while (chat_server != server_list_iter->value.End() && !succ)
    {
        if (!chat_server->IsObject())
        {
            chat_server++;
            continue;
        }
        auto char_server_host_iter = chat_server->FindMember("host");
        auto char_server_port_iter = chat_server->FindMember("wss_port");
        if (char_server_host_iter == chat_server->MemberEnd() || !char_server_host_iter->value.IsString()
            || char_server_port_iter == chat_server->MemberEnd() || !char_server_port_iter->value.IsInt())
        {
            chat_server++;
            continue;
        }

        auto token = std::string(token_iter->value.GetString(), token_iter->value.GetStringLength());
        auto host = std::string(char_server_host_iter->value.GetString(), char_server_host_iter->value.GetStringLength());
        auto port = char_server_port_iter->value.GetInt();
        SPDLOG_DEBUG("[bili_token_upd] Received new bilibili live chat config. token={}, host={}, port={}", token, host, port);

        on_success(bilibili_live_config{host, port, token});
        return;
    }
    if (!succ)
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: port or host doesn't exist or valid.");
        return on_failed();
    }
}

void async_fetch_bilibili_live_config(
    io_context& context,
    config::config_t config,
    int room_id,
    std::function<void(bilibili_live_config const&)> on_success,
    std::function<void()> on_failed)
{
    ip::tcp::resolver resolver(context);
    auto query_string = fmt::format("?room_id={}&platform=pc&player=web", room_id);

    UriUriA uri;
    const char* errorPos;
    auto& raw_url = (*config)["chat-config-url"].as<std::string>();
    auto retval = uriParseSingleUriA(&uri, raw_url.c_str(), &errorPos);
    if (retval != URI_SUCCESS)
        return spdlog::error("[live_cfg] Failed to parse chat config url {}!!!!!! urlparser Err:{}", raw_url, retval);

    auto port = std::string(uri.portText.first, uri.portText.afterLast);
    if (port.empty())
        port = "443";
    auto host = std::string(uri.hostText.first, uri.hostText.afterLast);
    auto endpoint = path_seg_to_string(uri.pathHead, "/") + query_string;

    resolver.async_resolve(host, port, [=, &context](const error_code& ec, const ip::tcp::resolver::results_type& resolved) -> void {
        if (ec)
            return on_failed(), spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config. err: {}:{}", room_id, ec.value(), ec.message());
        std::shared_ptr<ssl_stream<tcp_stream>> stream =
            std::make_shared<ssl_stream<tcp_stream>>(context, *ssl_context);
        stream->next_layer().expires_after(std::chrono::seconds((*config)["chat-config-timeout-sec"].as<int>()));
        if (!SSL_set_tlsext_host_name(stream->native_handle(), host.c_str()))
        {
            error_code ec{static_cast<int>(ERR_get_error()), boost::asio::error::get_ssl_category()};
            on_failed();
            return spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config when setting up openssl. err: {}:{}", room_id, ec.value(), ec.message());
        }

        async_connect(stream->next_layer().socket(), resolved, [=](const error_code& ec, const ip::tcp::resolver::endpoint_type&) -> void {
            if (ec)
                return on_failed(), spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config. err: {}:{}", room_id, ec.value(), ec.message());
            stream->async_handshake(ssl::stream_base::client, [=](const error_code& ec) -> void
            {
                if (ec)
                {
                    stream->next_layer().close();
                    return on_failed(), spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config when performing SSL handshake. err: {}:{}", room_id, ec.value(), ec.message());
                }

                std::shared_ptr<http::request<http::empty_body>> request = std::make_shared<http::request<http::empty_body>>();
                request->version(11);
                request->target(endpoint);
                request->method(http::verb::get);
                request->set(http::field::host, host);
                request->set(http::field::accept, "application/json");
                request->set(http::field::user_agent, (*config)["chat-config-user-agent"].as<std::string>());
                request->set(http::field::referer, (*config)["chat-config-referer"].as<std::string>());

                http::async_write(*stream, *request, [=](const error_code& ec, size_t) -> void {
                    // ReSharper disable CppExpressionWithoutSideEffects
                    request.get(); // Force capture
                    // ReSharper restore CppExpressionWithoutSideEffects
                    if (ec)
                    {
                        stream->next_layer().close();
                        on_failed();
                        return spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config when writing request. err: {}:{}", room_id, ec.value(), ec.message());
                    }
                    auto buffer = new flat_buffer();
                    auto res = new http::response<http::string_body>();
                    http::async_read(*stream, *buffer, *res, [=](const error_code& ec, size_t) -> void
                    {
                        if (ec)
                        {
                            delete buffer;
                            delete res;
                            stream->next_layer().close();
                            on_failed();
                            return spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config when receiving response. err: {}:{}", room_id, ec.value(), ec.message());
                        }
                        error_code ec2;
                        stream->shutdown(ec2);
                        if (res->result() != http::status::ok)
                        {
                            on_failed();
                            spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config. HTTP status:{}", room_id, res->result());
                            delete buffer;
                            delete res;
                            return;
                        }
                        parse_bilibili_config(res->body(), on_success, on_failed);
                        delete buffer;
                        delete res;
                    });
                });
            });
        });
    });

}
}
