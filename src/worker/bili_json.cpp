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

// TODO: 更好的命名
#define ASSERT_TRACE(expr)                                                   \
    if (!(expr))                                                             \
    {                                                                        \
        SPDLOG_TRACE("[bili_json] bilibili json type check failed: " #expr); \
        return false;                                                        \
    }

// 这个库似乎没有提供指定初始化容量的构造函数
robin_hood::unordered_map<string, function<bool(const Document&, RoomMessage*, Arena*)>> command;

CMD(DANMU_MSG)
{
    // TODO: 非结构错误的错误处理
    // TODO: 需要测试message使用完毕清空时embedded message是否会清空

    // 尽管所有UserMessage都需要设置UserInfo
    // 但是不同cmd的数据格式也有所不同
    // 因此设置UserInfo和设置RMQ的topic都只能强耦合在每个处理函数里

    ASSERT_TRACE(document.HasMember("info"))
    ASSERT_TRACE(document["info"].IsArray())
    // 以下变量均为 rapidjson::GenericArray ?
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
    // 以下变量类型均为 T*
    auto embedded_user_message = Arena::CreateMessage<live::UserMessage>(arena);
    auto embedded_user_info = Arena::CreateMessage<live::UserInfo>(arena);
    auto embedded_danmaku = Arena::CreateMessage<live::DanmakuMessage>(arena);

    // user_info
    ASSERT_TRACE(user_info[0].IsUint64())
    // uid
    embedded_user_info->set_uid(user_info[0].GetUint64());
    ASSERT_TRACE(user_info[1].IsString())
    // uname
    embedded_user_info->set_name(user_info[1].GetString(), user_info[1].GetStringLength());
    ASSERT_TRACE(user_info[2].IsBool())
    // admin
    embedded_user_info->set_admin(user_info[2].GetBool());
    ASSERT_TRACE(user_info[3].IsBool() && user_info[4].IsBool())
    // 这两个字段分别是月费/年费会员
    // 都为假时无会员 都为真时报错
    if (user_info[3].GetBool() == user_info[4].GetBool())
    {
        if (user_info[3].GetBool())
            ;  // 均为真 报错 TODO: 错误处理
        else   // 均为假 无直播会员
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
    ASSERT_TRACE(user_info[5].IsNumber())
    if (10000 == user_info[5].GetInt())
    {
        embedded_user_info->set_regular_user(true);
    }
    else if (5000 == user_info[5].GetInt())
    {
        embedded_user_info->set_regular_user(false);
    }
    else
    {
        // TODO: 错误处理
    }
    ASSERT_TRACE(user_info[6].IsBool())
    // phone_verified
    embedded_user_info->set_phone_verified(user_info[6].GetBool());

    // danmaku
    // 设置字符串的函数会分配额外的string 并且不通过arena分配内存
    // 需要考虑tcmalloc等
    ASSERT_TRACE(info[1].IsString())
    // message
    embedded_danmaku->set_message(info[1].GetString(), info[1].GetStringLength());
    ASSERT_TRACE(basic_info[9].IsUint())
    // danmaku type
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
        // TODO: 错误处理
        break;
    }
    ASSERT_TRACE(info[7].IsUint())
    // guard level
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
        // TODO: 错误处理
        break;
    }

    embedded_user_message->set_allocated_user(embedded_user_info);
    embedded_user_message->set_allocated_danmaku(embedded_danmaku);
    message->set_allocated_user_message(embedded_user_message);
    return true;
}

CMD(SUPER_CHAT_MESSAGE)
{
    // TODO: 更好的命名
    auto const& _data = document["data"];
    auto const& _user_info = _data["user_info"];
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
