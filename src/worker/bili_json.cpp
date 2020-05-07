#include "bili_json.h"

#include "borrowed_buffer.h"
#include "vNerve/bilibili/live/room_message.pb.h"
#include "vNerve/bilibili/live/user_message.pb.h"

#include <robin_hood.h>
#include <boost/thread/tss.hpp>
#include <rapidjson/allocators.h>
#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <google/protobuf/arena.h>
#include <spdlog/spdlog.h>

#include <functional>
#include <string>
#include <string_view>
#include <utility>

using std::function;
using std::string;

using google::protobuf::Arena;
using vNerve::bilibili::live::RoomMessage;

using MemoryPoolAllocator = rapidjson::MemoryPoolAllocator<>;
using Document = rapidjson::GenericDocument<rapidjson::UTF8<>, MemoryPoolAllocator, MemoryPoolAllocator>;

namespace vNerve::bilibili
{
// 需要可配置缓存大小吗
const size_t JSON_BUFFER_SIZE = 128 * 1024;
const size_t PARSE_BUFFER_SIZE = 32 * 1024;

#define CMD(name)                                                         \
    bool cmd_##name(const Document&, RoomMessage*, Arena*);               \
    bool cmd_##name##_inited = command.emplace(#name, cmd_##name).second; \
    bool cmd_##name(const Document& document, RoomMessage* message, Arena* arena)

#define ASSERT_TRACE(expr)                                                   \
    if (!(expr))                                                             \
    {                                                                        \
        SPDLOG_TRACE("[bili_json] bilibili json type check failed: " #expr); \
        return false;                                                        \
    }

robin_hood::unordered_map<string, function<bool(const Document&, RoomMessage*, Arena*)>> command;

CMD(DANMU_MSG)
{
    // TODO: 需要测试message使用完毕清空时embedded message是否会清空

    // 尽管所有UserMessage都需要设置UserInfo
    // 但是不同cmd的数据格式也有所不同
    // 因此设置UserInfo和设置RMQ的topic都只能强耦合在每个处理函数里

    // 以下变量均为 rapidjson::GenericArray ?
    ASSERT_TRACE(document.HasMember("info"))
    ASSERT_TRACE(document["info"].IsArray())
    // 数组数量不对是结构性错误
    // 但b站很有可能在不改变先前字段的情况下添加字段
    ASSERT_TRACE(document["info"].MemberCount() >= 15);
    auto const& info = document["info"];  // 或 document["info"].GetArray() ?
    ASSERT_TRACE(info[0].IsArray())
    ASSERT_TRACE(info[0].MemberCount() >= 11)
    auto const& basic_info = info[0];
    ASSERT_TRACE(info[2].IsArray())
    ASSERT_TRACE(info[2].MemberCount() >= 8)
    auto const& user_info = info[2];
    ASSERT_TRACE(info[3].IsArray())
    ASSERT_TRACE(info[3].MemberCount() >= 6)
    auto const& medal_info = info[3];
    ASSERT_TRACE(info[4].IsArray())
    ASSERT_TRACE(info[4].MemberCount() >= 4)
    auto const& user_level = info[4];
    // 以下变量类型均为 T*
    auto embedded_user_message = Arena::CreateMessage<live::UserMessage>(arena);
    auto embedded_user_info = Arena::CreateMessage<live::UserInfo>(arena);
    auto embedded_danmaku = Arena::CreateMessage<live::DanmakuMessage>(arena);
    auto embedded_medal_info = Arena::CreateMessage<live::MedalInfo>(arena);

    // user_info
    // uid
    ASSERT_TRACE(user_info[0].IsUint64())
    embedded_user_info->set_uid(user_info[0].GetUint64());
    // uname
    ASSERT_TRACE(user_info[1].IsString())
    embedded_user_info->set_name(user_info[1].GetString(), user_info[1].GetStringLength());
    // admin
    ASSERT_TRACE(user_info[2].IsBool())
    embedded_user_info->set_admin(user_info[2].GetBool());
    // vip&svip->livevip
    ASSERT_TRACE(user_info[3].IsBool() && user_info[4].IsBool())
    // 这两个字段分别是月费/年费会员
    // 都为假时无会员 都为真时报错
    if (user_info[3].GetBool() == user_info[4].GetBool())
    {
        if (user_info[3].GetBool())
            // 均为真 报错
            // 未设置的protobuf字段会被置为默认值
            SPDLOG_TRACE("[bili_json] both vip and svip are true");
        else  // 均为假 无直播会员
            embedded_user_info->set_vip_level(live::LiveVipLevel::NO_VIP);
    }
    else
    {
        if (user_info[3].GetBool())
            // 月费会员为真 年费会员为假 月费
            embedded_user_info->set_vip_level(live::LiveVipLevel::MONTHLY);
        else  // 月费会员为假 年费会员为真 年费
            embedded_user_info->set_vip_level(live::LiveVipLevel::YEARLY);
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
    ASSERT_TRACE(user_info[6].IsBool())
    embedded_user_info->set_phone_verified(user_info[6].GetBool());
    // user_level
    ASSERT_TRACE(user_level[0].IsUint())
    embedded_user_info->set_user_level(user_level[0].GetUint());
    // user_level_border_color
    ASSERT_TRACE(user_level[2].IsUint())
    embedded_user_info->set_user_level(user_level[2].GetUint());
    // title
    ASSERT_TRACE(info[5].IsArray())
    ASSERT_TRACE(info[5][1].IsString())
    embedded_user_info->set_title(info[5][1].GetString(), info[5][1].GetStringLength());
    // avatar_url
    // 弹幕没有头像字段 默认置空
    // main_vip
    // 弹幕没有主站vip字段 默认置空
    // user_level[1] unknown

    // medal
    // medal_name
    ASSERT_TRACE(medal_info[1].IsString())
    embedded_medal_info->set_medal_name(medal_info[1].GetString(), medal_info[1].GetStringLength());
    // medal_level
    ASSERT_TRACE(medal_info[0].IsUint())
    embedded_medal_info->set_medal_level(medal_info[0].GetUint());
    // medal_color
    ASSERT_TRACE(medal_info[5].IsUint())
    embedded_medal_info->set_medal_color(medal_info[5].GetUint());
    // liver user name
    ASSERT_TRACE(medal_info[2].IsString())
    embedded_medal_info->set_streamer_uname(medal_info[2].GetString(), medal_info[2].GetStringLength());
    // liver room id
    ASSERT_TRACE(medal_info[3].IsUint())
    embedded_medal_info->set_streamer_roomid(medal_info[3].GetUint());
    // liver user id
    // 弹幕没有牌子属于的用户的uid的字段
    // special_medal
    // medal_info[6] unknown
    // medal_info[7] unknown

    // danmaku
    // message
    // 设置字符串的函数会分配额外的string 并且不通过arena分配内存
    // 需要考虑tcmalloc等
    ASSERT_TRACE(info[1].IsString())
    embedded_danmaku->set_message(info[1].GetString(), info[1].GetStringLength());
    // danmaku type
    ASSERT_TRACE(basic_info[9].IsUint())
    switch (basic_info[9].GetUint())
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
    ASSERT_TRACE(info[7].IsUint())
    switch (info[7].GetUint())
    {
    case 0:  // 无舰队
        embedded_danmaku->set_guard_level(live::GuardLevel::NO_GUARD);
        break;
    case 1:  // 总督
        embedded_danmaku->set_guard_level(live::GuardLevel::LEVEL3);
        break;
    case 2:  // 提督
        embedded_danmaku->set_guard_level(live::GuardLevel::LEVEL2);
        break;
    case 3:  // 舰长
        embedded_danmaku->set_guard_level(live::GuardLevel::LEVEL1);
    default:
        SPDLOG_TRACE("[bili_json] unknown guard level");
    }

    embedded_user_info->set_allocated_medal(embedded_medal_info);
    embedded_user_message->set_allocated_user(embedded_user_info);
    embedded_user_message->set_allocated_danmaku(embedded_danmaku);
    message->set_allocated_user_message(embedded_user_message);
    return true;
}

CMD(SUPER_CHAT_MESSAGE)
{
    // TODO: 补充SC中的字段

    // 以下变量均为 rapidjson::GenericArray ?
    ASSERT_TRACE(document.HasMember("data"))
    ASSERT_TRACE(document["data"].IsObject())
    auto const& data = document["data"];
    ASSERT_TRACE(data.HasMember("user_info"))
    ASSERT_TRACE(data["user_info"].IsObject())
    auto const& user_info = data["user_info"];
    ASSERT_TRACE(data.HasMember("medal_info"))
    ASSERT_TRACE(data["medal_info"].IsObject())
    auto const& medal_info = data["medal_info"];
    // 以下变量类型均为 T*
    auto embedded_user_message = Arena::CreateMessage<live::UserMessage>(arena);
    auto embedded_user_info = Arena::CreateMessage<live::UserInfo>(arena);
    auto embedded_medal_info = Arena::CreateMessage<live::MedalInfo>(arena);
    auto embedded_superchat = Arena::CreateMessage<live::SuperChatMessage>(arena);

    // user_info
    // uid
    ASSERT_TRACE(data.HasMember("uid"))
    ASSERT_TRACE(data["uid"].IsUint64())
    embedded_user_info->set_uid(data["uid"].GetUint64());
    // uname
    ASSERT_TRACE(user_info.HasMember("uname"));
    ASSERT_TRACE(user_info["uname"].IsString())
    embedded_user_info->set_name(user_info["uname"].GetString(), user_info["uname"].GetStringLength());
    // admin
    ASSERT_TRACE(user_info.HasMember("manager"));
    ASSERT_TRACE(user_info["manager"].IsBool())
    embedded_user_info->set_admin(user_info["manager"].GetBool());
    // vip&svip->livevip
    ASSERT_TRACE(user_info.HasMember("is_vip")
                 & user_info.HasMember("is_svip")
                 & user_info["is_vip"].IsBool()
                 & user_info["is_svip"].IsBool())
    // 这两个字段分别是月费/年费会员
    // 都为假时无会员 都为真时报错
    if (user_info["is_vip"].GetBool() == user_info["is_svip"].GetBool())
    {
        if (user_info["is_vip"].GetBool())
            // 均为真 报错
            // 未设置的protobuf字段会被置为默认值
            SPDLOG_TRACE("[bili_json] both vip and svip are true");
        else  // 均为假 无直播会员
            embedded_user_info->set_vip_level(live::LiveVipLevel::NO_VIP);
    }
    else
    {
        if (user_info["is_vip"].GetBool())
            // 月费会员为真 年费会员为假 月费
            embedded_user_info->set_vip_level(live::LiveVipLevel::MONTHLY);
        else  // 月费会员为假 年费会员为真 年费
            embedded_user_info->set_vip_level(live::LiveVipLevel::YEARLY);
    }
    // regular user
    // phone_verified
    // SC似乎没有这些字段
    // 但是赠送礼物的理应可以视为正常用户 而不是默认值的非正常用户
    embedded_user_info->set_regular_user(true);
    embedded_user_info->set_phone_verified(true);
    ASSERT_TRACE(user_info[6].IsBool())
    // user_level
    ASSERT_TRACE(user_info.HasMember("user_level"))
    ASSERT_TRACE(user_level["user_level"].IsUint())
    embedded_user_info->set_user_level(user_level["user_level"].GetUint());
    // user_level_border_color
    // SC似乎没有这个字段
    // title
    // 值得注意的是 弹幕和礼物的title默认值是空字符串""
    // 但SC的title默认值是"0"
    // 需要考虑是否进行处理
    ASSERT_TRACE(user_info.HasMember("title"))
    ASSERT_TRACE(user_info["title"].IsString())
    embedded_user_info->set_title(user_info["title"].GetString(), user_info["title"].GetStringLength());
    ASSERT_TRACE(user_info.HasMember("is_main_vip"))
    // avatar_url
    ASSERT_TRACE(user_info.HasMember("face"))
    ASSERT_TRACE(user_info["face"].IsString())
    embedded_user_info->set_avatar_url(user_info["face"].GetString(), user_info["face"].GetStringLength());
    // user_info["face_frame"] 舰长框
    // 没有对应的字段
    // main_vip
    ASSERT_TRACE(user_info["is_main_vip"].IsBool())
    embedded_user_info->set_main_vip(user_info["is_main_vip"].GetBool());

    // medal
    // medal_name
    ASSERT_TRACE(medal_info.HasMember("medal_name"))
    ASSERT_TRACE(medal_info["medal_name"].IsString())
    embedded_medal_info->set_medal_name(medal_info["medal_name"].GetString(), medal_info["medal_name"].GetStringLength());
    // medal_level
    ASSERT_TRACE(medal_info.HasMember("medal_level"))
    ASSERT_TRACE(medal_info["medal_level"].IsUint())
    embedded_medal_info->set_medal_level(medal_info["medal_level"].GetUint());
    // medal_color
    ASSERT_TRACE(medal_info.HasMember("medal_color"))
    ASSERT_TRACE(medal_info["medal_color"].IsUint())
    embedded_medal_info->set_medal_color(medal_info["medal_color"].GetUint());
    // liver user id
    ASSERT_TRACE(medal_info.HasMember("target_id"))
    ASSERT_TRACE(medal_info["target_id"].IsUint())  // IsUint->Uint64存疑
    embedded_medal_info->set_streamer_uid(medal_info["target_id"].GetUint64());
    // liver user name
    ASSERT_TRACE(medal_info.HasMember("anchor_uname"))
    ASSERT_TRACE(medal_info["anchor_uname"].IsString())
    embedded_medal_info->set_streamer_uname(medal_info["anchor_uname"].GetString(), medal_info["anchor_uname"].GetStringLength());
    // liver room id
    ASSERT_TRACE(medal_info.HasMember("anchor_roomid"))
    ASSERT_TRACE(medal_info["anchor_roomid"].IsUint())
    embedded_medal_info->set_streamer_roomid(medal_info["anchor_roomid"].GetUint());
    // special_medal
    // medal_info[6] unknown
    // medal_info[7] unknown

    // superchat
    // id
    ASSERT_TRACE(data.HasMember("id"))
    ASSERT_TRACE(data["id"].IsUint());
    embedded_superchat->set_id(data["id"].GetUint());
    // message
    ASSERT_TRACE(data.HasMember("message"))
    ASSERT_TRACE(data["message"].IsString())
    embedded_superchat->set_message(data["message"].GetString(), data["message"].GetStringLength());
    // price
    ASSERT_TRACE(data.HasMember("price"))
    ASSERT_TRACE(data["price"].IsUint())
    embedded_superchat->set_price(::google::protobuf::uint32 value)(data["price"].GetUint());
    // token
    ASSERT_TRACE(data.HasMember("token"))
    ASSERT_TRACE(data["token"].IsString())
    embedded_superchat->set_token(data["token"].GetString(), data["token"].GetStringLength());
    // lasting_time_sec
    ASSERT_TRACE(data.HasMember("time"))
    ASSERT_TRACE(data["time"].IsUint())
    embedded_superchat->set_lasting_time_sec(data["time"].Get);
    // start_time
    ASSERT_TRACE(data.HasMember("start_time"))
    ASSERT_TRACE(data["start_time"].IsUint64())
    embedded_superchat->set_start_time(data["start_time"].GetUint64());
    // end_time
    ASSERT_TRACE(data.HasMember("end_time"))
    ASSERT_TRACE(data["end_time"].IsUint64())
    embedded_superchat->set_end_time(data["end_time"].GetUint64());

    embedded_user_info->set_allocated_medal(embedded_medal_info);
    embedded_user_message->set_allocated_user(embedded_user_info);
    embedded_user_message->set_allocated_super_chat(embedded_superchat);
    message->set_allocated_user_message(embedded_user_message);

    return true;
}

CMD(SEND_GIFT)
{
    // 以下变量均为 rapidjson::GenericArray ?
    ASSERT_TRACE(document.HasMember("data"))
    ASSERT_TRACE(document["data"].IsObject())
    auto const& data = document["data"];
    // 以下变量类型均为 T*
    auto embedded_user_message = Arena::CreateMessage<live::UserMessage>(arena);
    auto embedded_user_info = Arena::CreateMessage<live::UserInfo>(arena);
    auto embedded_gift = Arena::CreateMessage<live::GiftMessage>(arena);
    // 礼物似乎没有牌子信息
    // auto embedded_medal_info = Arena::CreateMessage<live::MedalInfo>(arena);

    // TODO

    // 礼物似乎没有牌子信息
    // embedded_user_info->set_allocated_medal(embedded_medal_info);
    embedded_user_message->set_allocated_user(embedded_user_info);
    embedded_user_message->set_allocated_gift(embedded_gift);
    message->set_allocated_user_message(embedded_user_message);
    return true;
}

class borrowed_message : public borrowed_buffer
{
private:
    const RoomMessage* _message;

public:
    borrowed_message(RoomMessage* message)
        : _message(message) {}
    ~borrowed_message() {}
    size_t size() override { return _message->ByteSizeLong(); }
    void write(void* data, int size) override { _message->SerializeToArray(data, size); }
};

class parse_context
{
private:
    unsigned char _json_buffer[JSON_BUFFER_SIZE];
    unsigned char _parse_buffer[PARSE_BUFFER_SIZE];
    MemoryPoolAllocator _value_allocator;
    MemoryPoolAllocator _stack_allocator;
    Document _document;
    Arena _arena;
    RoomMessage* _message;
    borrowed_message _borrowed_message;

public:
    parse_context()
        : _value_allocator(&_json_buffer, JSON_BUFFER_SIZE),
          _stack_allocator(&_parse_buffer, PARSE_BUFFER_SIZE),
          _document(&_value_allocator, PARSE_BUFFER_SIZE, &_stack_allocator),
          // 类成员按声明顺序初始化
          // _arena({256, 8192}),
          _message(Arena::CreateMessage<RoomMessage>(&_arena)),
          _borrowed_message(_message)
    {
    }
    ///
    /// 用于处理拆开数据包获得的json。
    /// @param buf json的缓冲区，将在函数中复用。
    /// @param room_id 消息所在的房间号
    /// @return json转换为的protobuf序列化后的buffer。
    const borrowed_message* serialize(char* buf, const unsigned int& room_id)
    {
        _message->Clear();
        _document.ParseInsitu(buf);
        _message->set_room_id(room_id);
        // TODO: 使用boost::multiindex配合robin_hood::hash魔改robin_hood::unordered_map来避免无意义的内存分配
        if (!(_document.HasMember("cmd")
              && _document["cmd"].IsString()))
        {
            SPDLOG_TRACE("[bili_json] bilibili json cmd type check failed");
            return nullptr;
        }
        string cmd(_document["cmd"].GetString(), _document["cmd"].GetStringLength());
        if ((command.find(cmd) != command.end())
            && command[cmd](_document, _message, &_arena))
            return &_borrowed_message;
        // 下面的代码会拼接字符串 但当编译选项为release时 日志宏不会启用
        SPDLOG_TRACE("[bili_json] bilibili json unknown cmd field: " + cmd);
        return nullptr;
    }
    ~parse_context();
};

boost::thread_specific_ptr<parse_context> _parse_context;

parse_context* get_parse_context()
{
    if (!_parse_context.get())
        _parse_context.reset(new parse_context);
    return _parse_context.get();
}

const borrowed_buffer* serialize_buffer(char* buf, const unsigned int& room_id)
{
    return get_parse_context()->serialize(buf, room_id);
}

}  // namespace vNerve::bilibili
