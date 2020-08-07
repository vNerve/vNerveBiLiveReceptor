#include "bilibili_live_config.h"

#include <functional>
#include <boost/bind.hpp>
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

class bilibili_live_config_fetch_context : public std::enable_shared_from_this<bilibili_live_config_fetch_context>
{
private:
    io_context* _context;
    int _room_id;
    std::string _host;
    std::string _endpoint;
    std::string const* _user_agent;
    std::function<void(bilibili_live_config const&)> _on_success;
    std::function<void()> _on_failed;
    config::config_t _config;

    std::unique_ptr<ssl_stream<tcp_stream>> _stream;
    std::unique_ptr<http::request<http::empty_body>> _request;
    flat_buffer _buffer;
    http::response<http::string_body> _response;

    void on_resolved(const error_code& ec, const ip::tcp::resolver::results_type& resolved);
    void on_connected(const error_code& ec);
    void on_ssl_handshake(const error_code& ec);
    void on_http_req_written(const error_code& ec);
    void on_response(const error_code& ec);

    void parse_bilibili_config();
public:
    bilibili_live_config_fetch_context(io_context* context, int room_id, config::config_t config,
                                       std::function<void(bilibili_live_config const&)> on_success, std::function<void()> on_failed,
                                       std::string host, std::string endpoint, std::string const* user_agent)
        : _context(context), _room_id(room_id), _host(std::move(host)), _endpoint(std::move(endpoint)),
    _user_agent(user_agent), _on_success(std::move(on_success)), _on_failed(std::move(on_failed)), _config(config)
    {
    }
    void init(ip::tcp::resolver& resolver, std::string const& port);
};

void bilibili_live_config_fetch_context::on_resolved(const error_code& ec, const ip::tcp::resolver::results_type& resolved)
{
    if (ec)
        return _on_failed(), spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config. Can't resolve. err: {}:{}",
            _room_id, ec.value(), ec.message());
    _stream = std::make_unique<ssl_stream<tcp_stream>>(*_context, *ssl_context);
    _stream->next_layer().expires_after(std::chrono::seconds((*_config)["chat-config-timeout-sec"].as<int>()));
    if (!SSL_set_tlsext_host_name(_stream->native_handle(), _host.c_str()))
    {
        error_code ec2 {static_cast<int>(ERR_get_error()), boost::asio::error::get_ssl_category()};
        _on_failed();
        return spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config when setting up openssl. err: {}:{}",
            _room_id, ec2.value(), ec2.message());
    }

    async_connect(_stream->next_layer().socket(), resolved,
                  boost::bind(&bilibili_live_config_fetch_context::on_connected, shared_from_this(), placeholders::error));
}

void bilibili_live_config_fetch_context::on_connected(const error_code& ec)
{
    if (ec)
        return _on_failed(), spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config can't connect. err: {}:{}",
            _room_id, ec.value(), ec.message());
    _stream->async_handshake(ssl::stream_base::client,
                             boost::bind(&bilibili_live_config_fetch_context::on_ssl_handshake, shared_from_this(), placeholders::error));
}

void bilibili_live_config_fetch_context::on_ssl_handshake(const error_code& ec)
{
    if (ec)
    {
        _stream->next_layer().close();
        return _on_failed(), spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config when performing SSL handshake. err: {}:{}",
            _room_id, ec.value(), ec.message());
    }

    _request = std::make_unique<http::request<http::empty_body>>();
    _request->version(11);
    _request->target(_endpoint);
    _request->method(http::verb::get);
    _request->set(http::field::host, _host);
    _request->set(http::field::accept, "application/json");
    _request->set(http::field::user_agent, _user_agent);
    _request->set(http::field::referer, (*_config)["chat-config-referer"].as<std::string>());

    http::async_write(*_stream, *_request,
        boost::bind(&bilibili_live_config_fetch_context::on_http_req_written, shared_from_this(), placeholders::error));
}

void bilibili_live_config_fetch_context::on_http_req_written(const error_code& ec)
{
    if (ec)
    {
        _stream->next_layer().close();
        _on_failed();
        return spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config when writing request. err: {}:{}",
            _room_id, ec.value(), ec.message());
    }
    http::async_read(*_stream, _buffer, _response,
        boost::bind(&bilibili_live_config_fetch_context::on_response, shared_from_this(), placeholders::error));
}

void bilibili_live_config_fetch_context::on_response(const error_code& ec)
{
    if (ec)
    {
        _stream->next_layer().close();
        _on_failed();
        return spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config when receiving response. err: {}:{}",
            _room_id, ec.value(), ec.message());
    }
    error_code ec2;
    _stream->shutdown(ec2);
    if (_response.result() != http::status::ok)
    {
        _on_failed();
        spdlog::warn("[live_cfg] Failed connecting to room {}! Failed to fetch chat config. HTTP status:{}",
            _room_id, _response.result());
        return;
    }
    parse_bilibili_config();
}

void bilibili_live_config_fetch_context::parse_bilibili_config()
{
    using namespace rapidjson;

    Document document;
    ParseResult result = document.Parse(_response.body().c_str());
    if (result.IsError())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Failed to parse JSON:{}", result.Code());
        return _on_failed();
    }
    if (!document.IsObject())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Root element is not JSON object.");
        return _on_failed();
    }
    auto code_iter = document.FindMember("code");
    if (code_iter == document.MemberEnd() || !code_iter->value.IsInt())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: no response code.");
        return _on_failed();
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
        return _on_failed();
    }
    auto data_iter = document.FindMember("data");
    if (data_iter == document.MemberEnd() || !data_iter->value.IsObject())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: no data object.");
        return _on_failed();
    }

    auto token_iter = data_iter->value.FindMember("token");
    if (token_iter == data_iter->value.MemberEnd() || !token_iter->value.IsString())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Token does not exist or isn't string.");
        return _on_failed();
    }

    auto server_list_iter = data_iter->value.FindMember("host_list");
    if (server_list_iter == data_iter->value.MemberEnd() || !server_list_iter->value.IsArray())
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: Server list doesn't exist or isn't array.");
        return _on_failed();
    }
    if (server_list_iter->value.GetArray().Size() <= 0)
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: No chat server provided.");
        return _on_failed();
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
        SPDLOG_DEBUG("[bili_token_upd] Received new bilibili live chat config. token={}, host={}, port={}, ua={}", token, host, port, *_user_agent);

        _on_success(bilibili_live_config{host, port, token, *_user_agent});
        return;
    }
    if (!succ)
    {
        spdlog::warn("[bili_token_upd] Invalid Bilibili Live Chat Config Response: port or host doesn't exist or valid.");
        return _on_failed();
    }
}

void bilibili_live_config_fetch_context::init(ip::tcp::resolver& resolver, std::string const& port)
{
    resolver.async_resolve(_host, port,
                           boost::bind(&bilibili_live_config_fetch_context::on_resolved, shared_from_this(), placeholders::error, placeholders::iterator));
}

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

void async_fetch_bilibili_live_config(
    io_context& context,
    ip::tcp::resolver& resolver,
    config::config_t config,
    int room_id,
    std::function<void(bilibili_live_config const&)> on_success,
    std::function<void()> on_failed)
{
    auto const& user_agents = (*config)["chat-config-user-agent"].as<std::vector<std::string>>();
    std::random_device rd;
    std::mt19937 rand_engine(rd() * room_id);
    std::uniform_int_distribution<> ua_dist(0, user_agents.size() - 1);
    auto const& user_agent = user_agents[ua_dist(rand_engine)];

    UriUriA uri;
    const char* errorPos;
    auto raw_url = fmt::format((*config)["chat-config-url"].as<std::string>(), room_id);
    auto retval = uriParseSingleUriA(&uri, raw_url.c_str(), &errorPos);
    if (retval != URI_SUCCESS)
        return spdlog::error("[live_cfg] Failed to parse chat config url {}!!!!!! urlparser Err:{}", raw_url, retval);

    auto port = std::string(uri.portText.first, uri.portText.afterLast);
    if (port.empty())
        port = "443";
    auto host = std::string(uri.hostText.first, uri.hostText.afterLast);
    auto endpoint = path_seg_to_string(uri.pathHead, "/") + "?" + std::string(uri.query.first, uri.query.afterLast - uri.query.first);

    std::make_shared<bilibili_live_config_fetch_context>(&context, room_id, config, on_success, on_failed, host, endpoint, &user_agent)->init(resolver, port);
    uriFreeUriMembersA(&uri);
}
}
