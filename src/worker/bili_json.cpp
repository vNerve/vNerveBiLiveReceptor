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
    // TODO: 更好的变量命名
    auto const& _info = document["info"].GetArray().Begin();
    auto const& _basic_info = _info->GetArray().Begin();
    auto const& _user_info = (_info + 2)->GetArray().Begin();
    // 需要测试message使用完毕清空时embedded message是否会清空
    auto user_message = Arena::CreateMessage<live::UserMessage>(arena);
    auto user_info = Arena::CreateMessage<live::UserInfo>(arena);
    auto danmaku = Arena::CreateMessage<live::DanmakuMessage>(arena);
    // 设置字符串的函数使用reinterpret_cast转换char指针 并不通过arena分配内存
    // 因此需要注意document是否会在message使用完之前释放
    danmaku->set_message((_info + 1)->GetString(), (_info + 1)->GetStringLength());
    // 抽奖弹幕是哪个字段？
    danmaku->set_lottery_type(live::LotteryDanmakuType::);
    danmaku->set_from_guard();
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
    const borrowed_message* serialize(char* buf)
    {
        _document.ParseInsitu(buf);
        // TODO: _message->set_room_id(...);
        // if (_document["cmd"].IsString())
        // TODO: 使用boost::multiindex配合robin_hood::hash魔改robin_hood::unordered_map来避免无意义的内存分配
        // if (command.find(...) != -1)
        // command.find(string(_document["cmd"].GetString(), _document["cmd"].GetStringLength()))->second(_document, _message, &_arena);
        command[string(_document["cmd"].GetString(), _document["cmd"].GetStringLength())](_document, _message, &_arena);
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

const borrowed_buffer* serialize_buffer(char* buf)
{
    return get_parse_context()->serialize(buf);
}

}  // namespace vNerve::bilibili
