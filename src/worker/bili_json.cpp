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
#include <rapidjson/error/error.h>
#include <rapidjson/error/en.h>
#include <google/protobuf/arena.h>
#include <spdlog/spdlog.h>

#include <string>
#include <string_view>
#include <functional>
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

using command_handler = function<bool(const unsigned int&, const Document&, borrowed_bilibili_message&, Arena*)>;
util::unordered_map_string<command_handler> command;

class parse_context
{
private:
    unsigned char _json_buffer[JSON_BUFFER_SIZE];
    unsigned char _parse_buffer[PARSE_BUFFER_SIZE];
    // We should create rapidjson::Document every parse

    Arena _arena;
    borrowed_bilibili_message _borrowed_bilibili_message;

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
        if (cmd_func_iter->second(room_id, document, _borrowed_bilibili_message, &_arena))
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

#define CMD(name)                                                                                    \
    bool cmd_##name(const unsigned int&, const Document&, borrowed_bilibili_message&, Arena*);       \
    bool cmd_##name##_inited = command.emplace(#name, cmd_##name).second;                            \
    bool cmd_##name(const unsigned int& room_id, const Document& document, borrowed_bilibili_message& message, Arena* arena)

#define ASSERT_TRACE(expr)                                                   \
    if (!(expr))                                                             \
    {                                                                        \
        spdlog::warn("[bili_json] bilibili json type check failed: " #expr); \
        return false;                                                        \
    }
#define ROUTING_KEY(fmtstr)                                                  \
    auto [_, routing_key_size] = fmt::format_to_n(message.routing_key,       \
        worker_supervisor::routing_key_max_size, fmtstr, room_id);           \
    if (routing_key_size >= worker_supervisor::routing_key_max_size)         \
    {                                                                        \
        spdlog::warn("[bili_json] Routing key too long:" fmtstr, room_id);   \
        routing_key_size = worker_supervisor::routing_key_max_size - 1;      \
    }                                                                        \
    message.routing_key[routing_key_size] = '\0';


#define GetMemberCheck(src, name, expr)                                  \
    auto name##_iter = (src).FindMember(#name);                          \
    ASSERT_TRACE(name##_iter != (src).MemberEnd() /* name = src[name] */)\
    auto const& name = name##_iter->value;                               \
    ASSERT_TRACE(expr)

void set_guard_level(unsigned int level, live::UserInfo* user_info)
{
    switch (level)
    {
    case 0:  // 无舰队
        user_info->set_guard_level(live::GuardLevel::NO_GUARD);
        break;
    case 1:  // 总督
        user_info->set_guard_level(live::GuardLevel::LEVEL3);
        break;
    case 2:  // 提督
        user_info->set_guard_level(live::GuardLevel::LEVEL2);
        break;
    case 3:  // 舰长
        user_info->set_guard_level(live::GuardLevel::LEVEL1);
    default:
        SPDLOG_TRACE("[bili_json] unknown guard level");
    }
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
    // 这两个字段分别是月费/年费会员
    // 都为假时无会员 都为真时报错
    if (user_info[3].IsInt() == user_info[4].IsInt())
    {
        if (user_info[3].GetInt() == 1)
            // 均为真 报错
            // 未设置的 protobuf 字段会被置为默认值
            SPDLOG_TRACE("[bili_json] both vip and svip are true");
        else  // 均为假 无直播会员
            embedded_user_info->set_live_vip_level(live::LiveVipLevel::NO_VIP);
    }
    else
    {
        if (user_info[3].GetInt() == 1)
            // 月费会员为真 年费会员为假 月费
            embedded_user_info->set_live_vip_level(live::LiveVipLevel::MONTHLY_VIP);
        else  // 月费会员为假 年费会员为真 年费
            embedded_user_info->set_live_vip_level(live::LiveVipLevel::YEARLY_VIP);
    }
    // regular user
    ASSERT_TRACE(user_info[5].IsNumber())
    if (10000 == user_info[5].GetInt())
        embedded_user_info->set_regular_user(true);
    else if (5000 == user_info[5].GetInt())
        embedded_user_info->set_regular_user(false);
    else
        SPDLOG_TRACE("[bili_json] unknown user rank");
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
        SPDLOG_TRACE("[bili_json] unknown danmaku lottery type");
    }

    // guard level
    auto const& guard_level = info[7];
    ASSERT_TRACE(guard_level.IsUint())
    set_guard_level(guard_level.GetInt(), embedded_user_info);

    return true;
}

CMD(SUPER_CHAT_MESSAGE)
{
    ROUTING_KEY("blv.{}.sc")
    GetMemberCheck(document, data, document["data"].IsObject())
    GetMemberCheck(data, user_info, data["user_info"].IsObject())
    auto embedded_user_message = message._message->mutable_user_message();
    auto embedded_user_info = embedded_user_message->mutable_user();
    auto embedded_superchat = embedded_user_message->mutable_super_chat();

    // user_info
    // uid
    GetMemberCheck(data, uid, uid.IsUint64())
    embedded_user_info->set_uid(uid.GetUint64());
    // uname
    GetMemberCheck(user_info, uname, uname.IsString())
    embedded_user_info->set_name(uname.GetString(), uname.GetStringLength());
    // admin
    GetMemberCheck(user_info, manager, manager.IsInt())
    embedded_user_info->set_admin(manager.GetInt() == 1);

    // vip&svip->livevip
    GetMemberCheck(user_info, is_vip, is_vip.IsInt())
    GetMemberCheck(user_info, is_svip, is_svip.IsInt())
    // 这两个字段分别是月费/年费会员
    // 都为假时无会员 都为真时报错
    if (is_vip.GetInt() == is_svip.GetInt())
    {
        if (is_vip.GetInt())
            // 均为真 报错
            // 未设置的protobuf字段会被置为默认值
            SPDLOG_TRACE("[bili_json] both vip and svip are true");
        else  // 均为假 无直播会员
            embedded_user_info->set_live_vip_level(live::LiveVipLevel::NO_VIP);
    }
    else
    {
        if (is_vip.GetInt())
            // 月费会员为真 年费会员为假 月费
            embedded_user_info->set_live_vip_level(live::LiveVipLevel::MONTHLY_VIP);
        else  // 月费会员为假 年费会员为真 年费
            embedded_user_info->set_live_vip_level(live::LiveVipLevel::YEARLY_VIP);
    }
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
    if (level_color.GetStringLength() == 7)
    {
        auto level_color_ul = std::strtoul(level_color.GetString() + 1, nullptr, 16);
        embedded_user_info->set_user_level_border_color(level_color_ul);
    }

    // title
    // 值得注意的是 弹幕和礼物的title默认值是空字符串""
    // 但SC的title默认值是"0"
    // 需要考虑是否进行处理
    GetMemberCheck(user_info, title, title.IsString())
    if (std::strncmp(title.GetString(), "0", 1) == 0)
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
    set_guard_level(guard_level.GetUint(), embedded_user_info);

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
        if (medal_color.GetStringLength() == 7)
        {
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
    GetMemberCheck(data, id, id.IsUint())
    embedded_superchat->set_id(id.GetUint());
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

    // gift
    GetMemberCheck(data, coin_type, coin_type.IsString())
    if (std::strncmp(
        "gold", coin_type.GetString(),
        std::min(4, static_cast<int>(coin_type.GetStringLength()))))
        embedded_gift->set_is_gold(true);
    else
        embedded_gift->set_is_gold(false);
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

}  // namespace vNerve::bilibili
