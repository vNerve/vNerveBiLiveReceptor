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

// #define CMD(name) robin_hood::pair<const string, function<void(const Document&, RoomMessage*, Arena*)>>(name, [](const Document& document, RoomMessage* message, Arena* arena)
// #define CMD_END() )
// // clang-format off
// const robin_hood::unordered_map<string, function<void(const Document&, RoomMessage*, Arena*)>> command({
//     CMD("DANMU_MSG")
//     {
//         auto const& info = document["info"].GetArray().Begin();
//         auto const& basic_info = info->GetArray().Begin();
//         auto const& user_info = (info + 2)->GetArray().Begin();
//     }
//     CMD_END(),
// });
// // clang-format on
// #undef CMD
// #undef CMD_END

#define CMD(name)                                                         \
    void cmd_##name(const Document&, RoomMessage*, Arena*);               \
    bool cmd_##name##_inited = command.emplace(#name, cmd_##name).second; \
    void cmd_##name(const Document& document, RoomMessage* message, Arena* arena)

// 这个库似乎没有提供指定初始化容量的构造函数
robin_hood::unordered_map<string, function<void(const Document&, RoomMessage*, Arena*)>> command;

CMD(DANMU_MSG)
{
    // TODO: 高强度检查
    // TODO: 错误处理
    // 以下变量类型均为 const rapidjson::GenericArray::Iterator&
    // 如何能够使用++iter并不产生内存开销？
    auto const& info_iter = document["info"].GetArray().Begin();
    auto const& basic_info_iter = info_iter->GetArray().Begin();
    auto const& user_info_iter = (info_iter + 2)->GetArray().Begin();
    // 需要测试message使用完毕清空时embedded message是否会清空
    // 以下变量类型均为 T*
    auto user_message = Arena::CreateMessage<live::UserMessage>(arena);
    auto user_info = Arena::CreateMessage<live::UserInfo>(arena);
    auto danmaku = Arena::CreateMessage<live::DanmakuMessage>(arena);

    // 尽管所有UserMessage都需要设置UserInfo
    // 但是不同cmd的数据格式也有所不同
    // 因此设置UserInfo和设置RMQ的topic都只能强耦合在每个处理函数里

    if (user_info_iter->IsUint64())  // uid
    {
        user_info->set_uid(user_info_iter->GetUint64());
    }
    else
    {
        // TODO: 错误处理
    }
    if ((user_info_iter + 1)->IsString())  // uname
    {
        user_info->set_name((user_info_iter + 1)->GetString(), (user_info_iter + 1)->GetStringLength());
    }
    else
    {
        // TODO: 错误处理
    }
    if ((user_info_iter + 2)->IsBool())  // admin
    {
        user_info->set_admin((user_info_iter + 2)->GetBool());
    }
    else
    {
        // TODO: 错误处理
    }
    if ((user_info_iter + 3)->IsBool() && (user_info_iter + 4)->IsBool())  // live vip
    {
        // 这两个字段分别是月费/年费会员
        // 都为假时无会员 都为真时报错
        if ((user_info_iter + 3)->GetBool() == (user_info_iter + 4)->GetBool())
        {
            if ((user_info_iter + 3)->GetBool())
            {
                // TODO: 错误处理
            }
            else
            {
                user_info->set_vip_level(live::LiveVipLevel::NO_VIP);
            }
        }
        else if ((user_info_iter + 3)->GetBool())
        {
            user_info->set_vip_level(live::LiveVipLevel::MONTHLY);
        }
        else
        {
            user_info->set_vip_level(live::LiveVipLevel::YEARLY);
        }
    }
    else
    {
        // TODO: 错误处理
    }
    if ((user_info_iter + 5)->IsNumber())
    {
        if (10000 == (user_info_iter + 5)->GetInt())
        {
            user_info->set_regular_user(true);
        }
        else if (5000 == (user_info_iter + 5)->GetInt())
        {
            user_info->set_regular_user(false);
        }
        else
        {
            // TODO: 错误处理
        }
    }
    else
    {
        // TODO: 错误处理
    }
    if ((user_info_iter + 6)->IsBool())
    {
        user_info->set_phone_verified((user_info_iter + 6)->GetBool());
    }
    else
    {
        // TODO: 错误处理
    }
    user_message->set_allocated_user(user_info);

    if ((info_iter + 1)->IsString())  // message
    {
        // 设置字符串的函数会分配额外的string 并且不通过arena分配内存
        // 需要考虑tcmalloc等
        danmaku->set_message((info_iter + 1)->GetString(), (info_iter + 1)->GetStringLength());
    }
    else
    {
        // TODO: 错误处理
    }
    if ((basic_info_iter + 9)->IsUint())  // danmaku type
    {
        switch ((basic_info_iter + 9)->GetUint())
        {
        case 0:  // 普通弹幕
            danmaku->set_lottery_type(live::LotteryDanmakuType::NO_LOTTERY);
            break;
        case 1:  // 节奏风暴
            danmaku->set_lottery_type(live::LotteryDanmakuType::STORM);
            break;
        case 2:  // 抽奖弹幕
            danmaku->set_lottery_type(live::LotteryDanmakuType::LOTTERY);
            break;
        default:
            // TODO: 错误处理
            break;
        }
    }
    else
    {
        // TODO: 错误处理
    }
    if ((info_iter + 7)->IsUint())  // guard level
    {
        switch ((info_iter + 7)->GetUint())
        {
        case 0:  // 无舰队
            danmaku->set_guard_level(live::GuardLevel::NO_GUARD);
            break;
        case 1:  // 总督
            danmaku->set_guard_level(live::GuardLevel::LEVEL3);
            break;
        case 2:  // 提督
            danmaku->set_guard_level(live::GuardLevel::LEVEL2);
            break;
        case 3:  // 舰长
            danmaku->set_guard_level(live::GuardLevel::LEVEL1);
        default:
            // TODO: 错误处理
            break;
        }
    }
    else
    {
        // TODO: 错误处理
    }
    user_message->set_allocated_danmaku(danmaku);
    message->set_allocated_user_message(user_message);
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
        _document.ParseInsitu(buf);
        _message->set_room_id(room_id);
        // TODO: 使用boost::multiindex配合robin_hood::hash魔改robin_hood::unordered_map来避免无意义的内存分配
        string cmd(_document["cmd"].GetString(), _document["cmd"].GetStringLength());
        // TODO: 简化太过分的检查
        // TODO: 针对不同的错误处理方式分拆if
        if (_document.HasMember("cmd")
            && _document["cmd"].IsString()
            && command.find(cmd) != command.end())
        {
            command[cmd](_document, _message, &_arena);
        }
        else
        {
            // TODO: 错误处理
        }
        return &_borrowed_message;
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
