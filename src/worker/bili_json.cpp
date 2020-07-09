#include "bili_json.h"

#include "borrowed_message.h"
#include "vNerve/bilibili/live/room_message.pb.h"
#include "vNerve/bilibili/live/user_message.pb.h"

#define CRCPP_USE_CPP11
#define CRCPP_BRANCHLESS
#include <CRC.h>
#include <robin_hood.h>
#include <boost/thread/tss.hpp>
#include <rapidjson/allocators.h>
#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <rapidjson/error/error.h>
#include <rapidjson/error/en.h>
#include <google/protobuf/arena.h>
#include <spdlog/spdlog.h>

#undef strtoull // fuck protobuf
#include <cstdlib>
#include <string>
#include <string_view>
#include <functional>
#include <locale>
#include <iomanip>
#include <sstream>
#include "robin_hood_ext.h"

using std::function;
using std::string;

using google::protobuf::Arena;
using vNerve::bilibili::live::RoomMessage;

using MemoryPoolAllocator = rapidjson::MemoryPoolAllocator<>;
using Document = rapidjson::GenericDocument<rapidjson::UTF8<>, MemoryPoolAllocator, MemoryPoolAllocator>;

namespace vNerve::bilibili
{
class borrowed_bilibili_message : public borrowed_message
{
public:
    // 友元必须在函数内声明 没法用宏批量定义
    // 只好从private挪进public 反正内部类无伤大雅
    RoomMessage* _message;

    borrowed_bilibili_message(RoomMessage* message)
        : _message(message) {}
    ~borrowed_bilibili_message() {}
    size_t size() const override { return _message->ByteSizeLong(); }
    void write(void* data, int size) const override { _message->SerializeToArray(data, size); }
};

const size_t JSON_BUFFER_SIZE = 128 * 1024;
const size_t PARSE_BUFFER_SIZE = 32 * 1024;
const CRC::Table<uint32_t, 32> crc_lookup_table(CRC::CRC_32());

class parse_context;
using command_handler = function<bool(const unsigned int&, const Document&, borrowed_bilibili_message&, Arena*, parse_context*)>;
util::unordered_map_string<command_handler> command;

class parse_context
{
private:
    unsigned char _json_buffer[JSON_BUFFER_SIZE];
    unsigned char _parse_buffer[PARSE_BUFFER_SIZE];
    // We should create rapidjson::Document every parse

    Arena _arena;
    borrowed_bilibili_message _borrowed_bilibili_message;
    rapidjson::StringBuffer _temp_string_buffer;

public:
    parse_context()
        : _borrowed_bilibili_message(Arena::CreateMessage<RoomMessage>(&_arena))
    {
    }
    ///
    /// 用于处理拆开数据包获得的json。
    /// @param buf json的缓冲区，将在函数中复用。
    /// @param length 原始json的长度，生成CRC时使用。
    /// @param room_id 消息所在的房间号。
    /// @return json转换为的protobuf序列化后的buffer。
    const borrowed_bilibili_message* serialize(char* buf, const size_t& length, const unsigned int& room_id)
    {
        _borrowed_bilibili_message._message->Clear();
        _borrowed_bilibili_message.crc32 = CRC::Calculate(buf, length, crc_lookup_table);  // 这个库又会做多少内存分配呢（已经不在乎了.

        MemoryPoolAllocator value_allocator(_json_buffer, JSON_BUFFER_SIZE);
        MemoryPoolAllocator stack_allocator(_parse_buffer, PARSE_BUFFER_SIZE);
        Document document(&value_allocator, PARSE_BUFFER_SIZE, &stack_allocator);
        rapidjson::ParseResult result = document.ParseInsitu(buf);
        if (result.IsError())
        {
            spdlog::warn("[bili_json] Bilibili JSON: Failed to parse JSON:{} ({})", rapidjson::GetParseError_En(result.Code()), result.Offset());
            return nullptr;
        }
        if (!document.IsObject())
        {
            spdlog::warn("[bili_json] Bilibili JSON: Root element is not JSON object.");
            return nullptr;
        }

        _borrowed_bilibili_message._message->set_room_id(room_id);
        auto cmd_iter = document.FindMember("cmd");
        if (cmd_iter == document.MemberEnd() || !cmd_iter->value.IsString())
        {
            SPDLOG_TRACE("[bili_json] bilibili json cmd type check failed: No cmd provided");
            return nullptr;
        }
        auto cmd_func_iter = command.find(std::string_view(cmd_iter->value.GetString(), cmd_iter->value.GetStringLength()));
        if (cmd_func_iter == command.end())
        {
            // 下面的代码会拼接字符串 但当编译选项为release时 日志宏不会启用
            SPDLOG_TRACE("[bili_json] bilibili json unknown cmd field: {}", cmd_iter->value.GetString());
            return nullptr;
        }
        if (cmd_func_iter->second(room_id, document, _borrowed_bilibili_message, &_arena, this))
            return &_borrowed_bilibili_message;

        SPDLOG_TRACE("[bili_json] Failed serializing message.");
        return nullptr;
    }

    const borrowed_bilibili_message* serialize(const long long int popularity, const unsigned int& room_id)
    {
        //_borrowed_bilibili_message.routing_key = "";
        _borrowed_bilibili_message.crc32 = 0; // see simple_worker_proto.h
        auto message = _borrowed_bilibili_message._message;
        auto [_, routing_key_size] = fmt::format_to_n(_borrowed_bilibili_message.routing_key,
            worker_supervisor::routing_key_max_size,
            "blv.{}.online", room_id);
        if (routing_key_size >= worker_supervisor::routing_key_max_size)
        {
            spdlog::warn(
                "[bili_json] Routing key too long:"
                "blv.{}.online",
                room_id);
            routing_key_size = worker_supervisor::routing_key_max_size - 1;
        }
        _borrowed_bilibili_message.routing_key[routing_key_size] = '\0';
        message->Clear();

        auto popularity_message = message->mutable_popularity_change();
        popularity_message->set_popularity(popularity);
        message->set_room_id(room_id);

        return &_borrowed_bilibili_message;
    }
    ~parse_context() {}

    rapidjson::StringBuffer& get_temp_string_buffer() { return _temp_string_buffer; }
};

boost::thread_specific_ptr<parse_context> _parse_context;

parse_context* get_parse_context()
{
    if (!_parse_context.get())
        _parse_context.reset(new parse_context);
    return _parse_context.get();
}

const borrowed_message* serialize_buffer(char* buf, const size_t& length, const unsigned int& room_id)
{
    return get_parse_context()->serialize(buf, length, room_id);
}

const borrowed_message* serialize_popularity(const long long popularity, const unsigned& room_id)
{
    return get_parse_context()->serialize(popularity, room_id);
}

std::string_view document_to_string(Document const& document, parse_context* context)
{
    auto& buffer = context->get_temp_string_buffer();
    buffer.Clear();
    rapidjson::Writer<std::decay<decltype(buffer)>::type> writer(buffer);
    document.Accept(writer);

    return std::string_view(buffer.GetString(), buffer.GetSize());
}

#define CMD(name)                                                                   \
    bool cmd_##name(const unsigned int&, const Document&,                           \
        borrowed_bilibili_message&, Arena*, parse_context*);                        \
    bool cmd_##name##_inited = command.emplace(#name, cmd_##name).second;           \
    bool cmd_##name(const unsigned int& room_id, const Document& document,          \
        borrowed_bilibili_message& message, Arena* arena, parse_context* context)

#define ASSERT_TRACE(expr)                                                   \
    if (!(expr))                                                             \
    {                                                                        \
        spdlog::warn("[bili_json] bilibili json type check failed: "         \
        #expr ": {}", document_to_string(document, context));                \
        return false;                                                        \
    }
#define ROUTING_KEY(fmtstr)                                                  \
    auto [_, routing_key_size] = fmt::format_to_n(message.routing_key,       \
        worker_supervisor::routing_key_max_size, fmtstr, room_id);           \
    if (routing_key_size >= worker_supervisor::routing_key_max_size)         \
    {                                                                        \
        SPDLOG_WARN("[bili_json] Routing key too long:" fmtstr, room_id);    \
        routing_key_size = worker_supervisor::routing_key_max_size - 1;      \
    }                                                                        \
    message.routing_key[routing_key_size] = '\0';


#define GetMemberCheck(src, name, expr)                                  \
    auto name##_iter = (src).FindMember(#name);                          \
    ASSERT_TRACE(name##_iter != (src).MemberEnd() /* name = src[name] */)\
    auto const& name = name##_iter->value;                               \
    ASSERT_TRACE(expr)

live::GuardLevel convert_guard_level(unsigned int level)
{
    switch (level)
    {
    case 0:  // 无舰队
        return live::GuardLevel::NO_GUARD;
    case 1:  // 总督
        return live::GuardLevel::LEVEL3;
    case 2:  // 提督
        return live::GuardLevel::LEVEL2;
    case 3:  // 舰长
        return live::GuardLevel::LEVEL1;
    default:
        SPDLOG_TRACE("[bili_json] unknown guard level");
        return live::GuardLevel::NO_GUARD;
    }
}

live::LiveVipLevel convert_live_vip_level(int vip, int svip)
{
    if (svip == 1)
        return live::LiveVipLevel::YEARLY_VIP;
    if (vip == 1)
        return live::LiveVipLevel::MONTHLY_VIP;
    return live::LiveVipLevel::NO_VIP;
}

CMD(DANMU_MSG)
{
    // 尽管所有UserMessage都需要设置UserInfo
    // 但是不同cmd的数据格式也有所不同
    // 因此设置UserInfo和设置RMQ的topic都只能强耦合在每个处理函数里
    ROUTING_KEY("blv.{}.danmaku")

    // 数组数量不对是结构性错误
    // 但b站很有可能在不改变先前字段的情况下添加字段

    GetMemberCheck(document, info, info.IsArray() && info.Size() >= 15)

    auto const& basic_info = info[0];
    ASSERT_TRACE(basic_info.IsArray() && basic_info.Size() >= 11 /* basic_info = info[0] */)

    auto const& user_info = info[2];
    ASSERT_TRACE(user_info.IsArray() && user_info.Size() >= 8 /* user_info = info[2] */)

    auto const& medal_info = info[3];
    auto medal_info_present = medal_info.IsArray() && medal_info.Size() >= 6;

    auto const& user_level_info = info[4];
    ASSERT_TRACE(user_level_info.IsArray() && user_level_info.Size() >= 4 /* user_level_info = info[4] */);

    auto const& title_info = info[5];
    auto title_info_present = title_info.IsArray() && title_info.Size() >= 2;

    // 以下变量类型均为 T*
    auto embedded_user_message = message._message->mutable_user_message();
    auto embedded_user_info = embedded_user_message->mutable_user();
    auto embedded_danmaku = embedded_user_message->mutable_danmaku();

    // user_info
    // uid
    ASSERT_TRACE(user_info[0].IsUint64())
    embedded_user_info->set_uid(user_info[0].GetUint64());
    // uname
    ASSERT_TRACE(user_info[1].IsString())
    embedded_user_info->set_name(user_info[1].GetString(), user_info[1].GetStringLength());
    // admin
    ASSERT_TRACE(user_info[2].IsInt())
    embedded_user_info->set_admin(user_info[2].GetInt() == 1);
    // vip&svip->livevip
    ASSERT_TRACE(user_info[3].IsInt() && user_info[4].IsInt())
    embedded_user_info->set_live_vip_level(
        convert_live_vip_level(user_info[3].GetInt(), user_info[4].GetInt()));
    // regular user
    ASSERT_TRACE(user_info[5].IsNumber())
    if (10000 == user_info[5].GetInt())
        embedded_user_info->set_regular_user(true);
    else if (5000 == user_info[5].GetInt())
        embedded_user_info->set_regular_user(false);
    else
        SPDLOG_WARN("[bili_json] unknown user rank");
    // phone_verified
    ASSERT_TRACE(user_info[6].IsInt())
    embedded_user_info->set_phone_verified(user_info[6].GetInt() == 1);
    // user_level
    ASSERT_TRACE(user_level_info[0].IsUint())
    embedded_user_info->set_user_level(user_level_info[0].GetUint());
    // user_level_border_color
    ASSERT_TRACE(user_level_info[2].IsUint())
    embedded_user_info->set_user_level(user_level_info[2].GetUint());
    // title
    if (title_info_present)
    {
        auto const& title = title_info[1];
        if (title.IsString())
            embedded_user_info->set_title(title.GetString(), title.GetStringLength());
    }

    // avatar_url
    // 弹幕没有头像字段 默认置空
    // main_vip
    // 弹幕没有主站vip字段 默认置空
    // user_level[1] unknown

    // medal
    if (medal_info_present)
    {
        auto embedded_medal_info = embedded_user_info->mutable_medal();
        // medal_name
        ASSERT_TRACE(medal_info[1].IsString())
        embedded_medal_info->set_medal_name(medal_info[1].GetString(), medal_info[1].GetStringLength());
        // medal_level
        ASSERT_TRACE(medal_info[0].IsUint())
        embedded_medal_info->set_medal_level(medal_info[0].GetUint());
        // medal_color
        ASSERT_TRACE(medal_info[4].IsUint())
        embedded_medal_info->set_medal_color(medal_info[4].GetUint());
        // liver user name
        ASSERT_TRACE(medal_info[2].IsString())
        embedded_medal_info->set_streamer_name(medal_info[2].GetString(), medal_info[2].GetStringLength());
        // liver room id
        ASSERT_TRACE(medal_info[3].IsUint())
        embedded_medal_info->set_streamer_roomid(medal_info[3].GetUint());
        // liver user id
        // 弹幕没有牌子属于的主播的uid的字段
        // special_medal
        // medal_info[6] unknown
        // medal_info[7] unknown
    }

    // danmaku
    // message
    // 设置字符串的函数会分配额外的string 并且不通过arena分配内存
    // 需要考虑tcmalloc等
    auto const& message_content = info[1];
    ASSERT_TRACE(message_content.IsString() /* message = info[1] */)
    embedded_danmaku->set_message(message_content.GetString(), message_content.GetStringLength());

    // danmaku type
    auto const& danmaku_type = basic_info[9];
    ASSERT_TRACE(danmaku_type.IsUint() /* danmaku_type = basic_info[9] */)
    switch (danmaku_type.GetUint())
    {
    case 0:  // 普通弹幕
        embedded_danmaku->set_lottery_type(live::LotteryDanmakuType::NO_LOTTERY);
        break;
    case 1:  // 节奏风暴
        embedded_danmaku->set_lottery_type(live::LotteryDanmakuType::STORM);
        break;
    case 2:  // 抽奖弹幕
        embedded_danmaku->set_lottery_type(live::LotteryDanmakuType::LOTTERY);
        break;
    default:
        SPDLOG_WARN("[bili_json] unknown danmaku lottery type");
    }

    // guard level
    auto const& guard_level = info[7];
    ASSERT_TRACE(guard_level.IsUint())
    embedded_user_info->set_guard_level(convert_guard_level(guard_level.GetInt()));

    return true;
}

CMD(SUPER_CHAT_MESSAGE)
{
    ROUTING_KEY("blv.{}.sc")
    GetMemberCheck(document, data, data.IsObject())
    GetMemberCheck(data, user_info, user_info.IsObject())
    auto embedded_user_message = message._message->mutable_user_message();
    auto embedded_user_info = embedded_user_message->mutable_user();
    auto embedded_superchat = embedded_user_message->mutable_super_chat();

    // user_info
    // uid
    GetMemberCheck(data, uid, uid.IsInt64())
    embedded_user_info->set_uid(uid.GetInt64());
    // uname
    GetMemberCheck(user_info, uname, uname.IsString())
    embedded_user_info->set_name(uname.GetString(), uname.GetStringLength());
    // admin
    GetMemberCheck(user_info, manager, manager.IsInt())
    embedded_user_info->set_admin(manager.GetInt() == 1);

    // vip&svip->livevip
    GetMemberCheck(user_info, is_vip, is_vip.IsInt())
    GetMemberCheck(user_info, is_svip, is_svip.IsInt())
    embedded_user_info->set_live_vip_level(convert_live_vip_level(is_vip.GetInt(), is_svip.GetInt()));
    // regular user
    // phone_verified
    // SC似乎没有这些字段
    // 但是赠送礼物的理应可以视为正常用户 而不是默认值的非正常用户
    embedded_user_info->set_regular_user(true);
    embedded_user_info->set_phone_verified(true);
    // user_level
    GetMemberCheck(user_info, user_level, user_level.IsUint())
    embedded_user_info->set_user_level(user_level.GetUint());
    // user_level_border_color
    GetMemberCheck(user_info, level_color, level_color.IsString())
    if (level_color.GetStringLength() >= 7)
    {
        // skip the leading '#'
        auto level_color_ul = std::strtoul(level_color.GetString() + 1, nullptr, 16);
        embedded_user_info->set_user_level_border_color(level_color_ul);
    }

    // title
    // 值得注意的是 弹幕和礼物的title默认值是空字符串""
    // 但SC的title默认值是"0"
    // 需要考虑是否进行处理
    using namespace std::literals;
    GetMemberCheck(user_info, title, title.IsString())
    if (title.GetString() == "0"sv)
        embedded_user_info->set_title("");
    else
        embedded_user_info->set_title(title.GetString(), title.GetStringLength());

    GetMemberCheck(user_info, is_main_vip, is_main_vip.IsInt())
    embedded_user_info->set_main_vip(is_main_vip == 1);

    // avatar_url
    GetMemberCheck(user_info, face, face.IsString())
    embedded_user_info->set_avatar_url(face.GetString(), face.GetStringLength());
    // user_info["face_frame"]
    // 没有对应的字段
    // guard_level
    GetMemberCheck(user_info, guard_level, guard_level.IsUint())
    embedded_user_info->set_guard_level(convert_guard_level(guard_level.GetInt()));

    // medal
    auto medal_info_iter = data.FindMember("medal_info");
    if (medal_info_iter != data.MemberEnd()
        && medal_info_iter->value.IsObject()
        && medal_info_iter->value.HasMember("target_id"))
    {
        auto const& medal_info = medal_info_iter->value;
        auto embedded_medal_info = embedded_user_info->mutable_medal();

        // medal_name
        GetMemberCheck(medal_info, medal_name, medal_name.IsString())
        embedded_medal_info->set_medal_name(medal_name.GetString(), medal_name.GetStringLength());
        // medal_level
        GetMemberCheck(medal_info, medal_level, medal_level.IsUint())
        embedded_medal_info->set_medal_level(medal_level.GetUint());
        // medal_color
        GetMemberCheck(medal_info, medal_color, medal_color.IsString())
        if (medal_color.GetStringLength() >= 7)
        {
            // skip the leading '#'
            auto medal_color_ul = std::strtoul(medal_color.GetString(), nullptr, 16);
            embedded_medal_info->set_medal_color(medal_color_ul);
        }

        // liver user id
        GetMemberCheck(medal_info, target_id, target_id.IsUint())
        embedded_medal_info->set_streamer_uid(target_id.GetUint64());
        // liver user name
        GetMemberCheck(medal_info, anchor_uname, anchor_uname.IsString())
        embedded_medal_info->set_streamer_name(anchor_uname.GetString(), anchor_uname.GetStringLength());
        // liver room id
        GetMemberCheck(medal_info, anchor_roomid, anchor_roomid.IsUint())
        embedded_medal_info->set_streamer_roomid(anchor_roomid.GetUint());
    }
    else
        embedded_user_info->clear_medal();

    // superchat
    // id
    GetMemberCheck(data, id, id.IsString())
    auto id_ull = std::strtoull(id.GetString(), nullptr, 10);
    embedded_superchat->set_id(id_ull);
    // message
    auto content_iter = data.FindMember("message");
    ASSERT_TRACE(content_iter != data.MemberEnd())
    auto const& content = content_iter->value;
    embedded_superchat->set_message(content.GetString(), content.GetStringLength());
    // price
    GetMemberCheck(data, price, price.IsInt())
    embedded_superchat->set_price_cny(price.GetUint());
    GetMemberCheck(data, rate, rate.IsUint())
    embedded_superchat->set_price_coin(price.GetUint() * rate.GetUint());
    // token
    GetMemberCheck(data, token, token.IsString())
    embedded_superchat->set_token(token.GetString(), token.GetStringLength());
    // lasting_time_sec
    GetMemberCheck(data, time, time.IsUint())
    embedded_superchat->set_lasting_time_sec(time.GetUint());
    // start_time
    GetMemberCheck(data, start_time, start_time.IsUint64())
    embedded_superchat->set_start_time(start_time.GetUint64());
    // end_time
    GetMemberCheck(data, end_time, start_time.IsUint64())
    embedded_superchat->set_start_time(end_time.GetUint64());

    message._message->set_allocated_user_message(embedded_user_message);

    return true;
}

CMD(SEND_GIFT)
{
    ROUTING_KEY("blv.{}.gift")

    // 以下变量均为 rapidjson::GenericArray ?
    GetMemberCheck(document, data, data.IsObject())
    // 以下变量类型均为 T*
    auto embedded_user_message = message._message->mutable_user_message();
    auto embedded_user_info = embedded_user_message->mutable_user();
    auto embedded_gift = embedded_user_message->mutable_gift();

    // user_info
    GetMemberCheck(data, uid, uid.IsUint64())
    embedded_user_info->set_uid(uid.GetUint64());
    GetMemberCheck(data, uname, uname.IsString())
    embedded_user_info->set_name(uname.GetString(), uname.GetStringLength());
    GetMemberCheck(data, face, face.IsString())
    embedded_user_info->set_avatar_url(face.GetString(), face.GetStringLength());

    using namespace std::literals;
    // gift
    GetMemberCheck(data, coin_type, coin_type.IsString())
    embedded_gift->set_is_gold(coin_type.GetString() == "gold"sv);
    GetMemberCheck(data, total_coin, total_coin.IsUint())
    embedded_gift->set_total_coin(total_coin.GetUint());
    GetMemberCheck(data, price, price.IsUint())
    embedded_gift->set_single_price_coin_raw(price.GetUint());

    GetMemberCheck(data, giftId, giftId.IsUint())
    embedded_gift->set_gift_id(giftId.GetUint());
    GetMemberCheck(data, giftName, giftName.IsString())
    embedded_gift->set_gift_name(giftName.GetString(), giftName.GetStringLength());
    GetMemberCheck(data, num, num.IsUint());
    embedded_gift->set_count(num.GetUint());

    return true;
}

CMD(USER_TOAST_MSG)
{
    ROUTING_KEY("blv.{}.new_guard")
    GetMemberCheck(document, data, data.IsObject())
    auto embedded_user_message = message._message->mutable_user_message();
    auto embedded_user_info = embedded_user_message->mutable_user();
    auto embedded_new_guard = embedded_user_message->mutable_new_guard();

    GetMemberCheck(data, uid, uid.IsUint64())
    embedded_user_info->set_uid(uid.GetUint64());
    GetMemberCheck(data, username, username.IsString())
    embedded_user_info->set_name(username.GetString(), username.GetStringLength());

    GetMemberCheck(data, guard_level, guard_level.IsInt())
    auto guard_level_converted = convert_guard_level(guard_level.GetInt());
    embedded_user_info->set_guard_level(guard_level_converted);
    embedded_new_guard->set_level(guard_level_converted);
    embedded_user_info->set_phone_verified(true); // 理由同sc
    embedded_user_info->set_regular_user(true);

    GetMemberCheck(data, op_type, op_type.IsInt())
    switch (op_type.GetInt())
    {
    case 1:
        embedded_new_guard->set_buy_type(live::GuardBuyType::BUY);
        break;
    case 2:
        embedded_new_guard->set_buy_type(live::GuardBuyType::RENEW);
        break;
    default:
        SPDLOG_WARN("[bili_json] Unknown guard oper type");
    }

    using namespace std::literals;
    GetMemberCheck(data, unit, unit.IsString())
    // 月
    if (unit.GetString() == "\xe6\x9c\x88"sv)
        embedded_new_guard->set_duration_level(live::GuardDurationLevel::MONTHLY_GUARD);
    // 周
    else if (unit.GetString() == "\xe5\x91\xa8"sv)
        embedded_new_guard->set_duration_level(live::GuardDurationLevel::WEEKLY_GUARD);
    else
        SPDLOG_WARN("[bili_json] Invalid guard duration {}!",
            std::string_view(unit.GetString(), unit.GetStringLength()));

    GetMemberCheck(data, num, num.IsInt())
    embedded_new_guard->set_count(num.GetInt());
    GetMemberCheck(data, price, price.IsUint());
    embedded_new_guard->set_total_coin(price.GetUint());

    return true;
}

CMD(WELCOME)
{
    ROUTING_KEY("blv.{}.welcome_vip")
    GetMemberCheck(document, data, data.IsObject())
    auto embedded_user_message = message._message->mutable_user_message();
    auto embedded_user_info = embedded_user_message->mutable_user();
    auto embedded_welcome_vip = embedded_user_message->mutable_welcome_vip();

    GetMemberCheck(data, uid, uid.IsUint64())
    embedded_user_info->set_uid(uid.GetUint64());
    GetMemberCheck(data, uname, uname.IsString())
    embedded_user_info->set_name(uname.GetString(), uname.GetStringLength());
    GetMemberCheck(data, is_admin, is_admin.IsBool())
    embedded_user_info->set_admin(is_admin.GetBool());

    GetMemberCheck(data, vip, vip.IsInt())
    GetMemberCheck(data, svip, svip.IsInt())
    embedded_welcome_vip->set_level(
        convert_live_vip_level(vip.GetInt(), svip.GetInt()));

    return true;
}

CMD(WELCOME_GUARD)
{
    ROUTING_KEY("blv.{}.welcome_guard")
    GetMemberCheck(document, data, data.IsObject())
    auto embedded_user_message = message._message->mutable_user_message();
    auto embedded_user_info = embedded_user_message->mutable_user();
    auto embedded_welcome_guard = embedded_user_message->mutable_welcome_guard();

    GetMemberCheck(data, uid, uid.IsUint64())
    embedded_user_info->set_uid(uid.GetUint64());
    GetMemberCheck(data, username, username.IsString())
    embedded_user_info->set_name(username.GetString(), username.GetStringLength());

    GetMemberCheck(data, guard_level, guard_level.IsInt())
    embedded_welcome_guard->set_level(
            convert_guard_level(guard_level.GetInt()));

    return true;
}

CMD(ROOM_BLOCK_MSG)
{
    ROUTING_KEY("blv.{}.user_blocked")
    //GetMemberCheck(document, data, data.IsObject())
    auto embedded_user_message = message._message->mutable_user_message();
    auto embedded_user_info = embedded_user_message->mutable_user();

    embedded_user_message->mutable_user_blocked();

    auto uid_iter = document.FindMember("uid");
    ASSERT_TRACE(uid_iter != document.MemberEnd())
    auto const& uid = uid_iter->value;
    if (uid.IsString())
    {
        auto uid_ull = std::strtoul(uid.GetString(), nullptr, 16);
        embedded_user_info->set_uid(uid_ull);
    }
    else if (uid.IsUint64())
        embedded_user_info->set_uid(uid.GetUint64());
    else
    {
        SPDLOG_WARN("[bili_json] bad uid type.");
        return false;
    }

    GetMemberCheck(document, uname, uname.IsString())
    embedded_user_info->set_name(uname.GetString(), uname.GetStringLength());

    return true;
}

CMD(LIVE)
{
    ROUTING_KEY("blv.{}.live_status")
    auto embedded_live_status = message._message->mutable_live_status();
    embedded_live_status->set_status(live::LiveStatus::LIVE);
    return true;
}

CMD(PREPARING)
{
    ROUTING_KEY("blv.{}.live_status")
    auto embedded_live_status = message._message->mutable_live_status();
    embedded_live_status->set_status(live::LiveStatus::PREPARING);
    return true;
}

CMD(ROUND)
{
    ROUTING_KEY("blv.{}.live_status")
    auto embedded_live_status = message._message->mutable_live_status();
    embedded_live_status->set_status(live::LiveStatus::ROUND);
    return true;
}

CMD(CUT_OFF)
{
    ROUTING_KEY("blv.{}.live_status")
    auto embedded_live_status = message._message->mutable_live_status();
    embedded_live_status->set_status(live::LiveStatus::CUT_OFF);

    auto msg_iter = document.FindMember("msg");
    if (msg_iter == document.MemberEnd())
    {
        auto const& msg = msg_iter->value;
        embedded_live_status->set_message(msg.GetString(), msg.GetStringLength());
    }
    return true;
}

CMD(ROOM_CHANGE)
{
    ROUTING_KEY("blv.{}.room_info")
    auto embedded_info_change = message._message->mutable_info_change();
    auto embedded_base_info = embedded_info_change->mutable_base_info();
    GetMemberCheck(document, data, data.IsObject())

    GetMemberCheck(data, title, title.IsString())
    embedded_base_info->set_title(title.GetString(), title.GetStringLength());

    GetMemberCheck(data, area_id, area_id.IsUint())
    embedded_base_info->set_area_id(area_id.GetUint());
    GetMemberCheck(data, area_name, area_name.IsString());
    embedded_base_info->set_area_name(area_name.GetString(), area_name.GetStringLength());

    GetMemberCheck(data, parent_area_id, parent_area_id.IsUint())
    embedded_base_info->set_parent_area_id(parent_area_id.GetUint());
    GetMemberCheck(data, parent_area_name, parent_area_name.IsString());
    embedded_base_info->set_parent_area_name(parent_area_name.GetString(), parent_area_name.GetStringLength());

    return true;
}

CMD(CHANGE_ROOM_INFO)
{
    ROUTING_KEY("blv.{}.room_info")
    auto embedded_info_change = message._message->mutable_info_change();

    GetMemberCheck(document, background, background.IsString())
    embedded_info_change->set_background_url(background.GetString(), background.GetStringLength());
    return true;
}

CMD(ROOM_SKIN_MSG)
{
    ROUTING_KEY("blv.{}.room_info")
    auto embedded_info_change = message._message->mutable_info_change();

    GetMemberCheck(document, skin_id, skin_id.IsUint())
    embedded_info_change->set_skin_id(skin_id.GetUint());
    return true;
}

CMD(ROOM_ADMINS)
{
    ROUTING_KEY("blv.{}.room_info")
    auto embedded_info_change = message._message->mutable_info_change();
    auto embedded_admin = embedded_info_change->mutable_admin();

    GetMemberCheck(document, uids, uids.IsArray())
    for (auto const& elem : uids.GetArray())
    {
        if (elem.IsUint64())
            embedded_admin->add_uid(elem.GetUint64());
    }
    return true;
}

CMD(ROOM_LOCK)
{
    ROUTING_KEY("blv.{}.room_locked")
    auto embedded_room_locked = message._message->mutable_room_locked();

    GetMemberCheck(document, expire, expire.IsString())
    std::tm expire_tm = {};
    std::istringstream ss(expire.GetString());

    if (ss >> std::get_time(&expire_tm, "%Y-%m-%d %H:%M:%S"))
    {
        auto timestamp = std::mktime(&expire_tm);
        embedded_room_locked->set_locked_until(timestamp);
    }
    else
        SPDLOG_WARN("[bili_json] Invalid expire date string: {}",
                    std::string_view(expire.GetString(), expire.GetStringLength()));
    return true;
}

CMD(WARNING)
{
    ROUTING_KEY("blv.{}.room_warning")
    auto embedded_room_warned = message._message->mutable_room_warning();

    GetMemberCheck(document, msg, msg.IsString())
    embedded_room_warned->set_message(msg.GetString(), msg.GetStringLength());

    return true;
}

CMD(ROOM_LIMIT)
{
    ROUTING_KEY("blv.{}.room_limited")
    auto embedded_room_limited = message._message->mutable_room_limited();

    GetMemberCheck(document, type, type.IsString())
    embedded_room_limited->set_type(type.GetString(), type.GetStringLength());
    GetMemberCheck(document, delay_range, delay_range.IsUint())
    embedded_room_limited->set_delay_range(delay_range.GetUint());

    return true;
}

CMD(SUPER_CHAT_MESSAGE_DELETE)
{
    ROUTING_KEY("blv.{}.sc_delete")
    auto embedded_super_chat_delete = message._message->mutable_superchat_delete();

    GetMemberCheck(document, data, data.IsObject())
    GetMemberCheck(data, ids, ids.IsArray())

    for (auto const& elem : ids.GetArray())
    {
        if (elem.IsUint64())
            embedded_super_chat_delete->add_id(elem.GetUint64());
    }

    return true;
}

}  // namespace vNerve::bilibili
