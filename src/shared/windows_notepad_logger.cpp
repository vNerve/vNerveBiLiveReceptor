#include <Windows.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>

#include <mutex>

namespace vNerve::bilibili::util
{
template <typename Mutex>
class notepad3_sink : public spdlog::sinks::base_sink<Mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<Mutex>::formatter_->format(msg, formatted);
        auto str = fmt::to_string(formatted);

        auto hwnd_notepad = FindWindowW(NULL, L"未命名 - Notepad3");
        if (!hwnd_notepad)
        {
            hwnd_notepad = FindWindowW(NULL, L"* 未命名 - Notepad3");
            if (!hwnd_notepad)
                return;
        }

        auto hwnd_edit = FindWindowExW(hwnd_notepad, NULL, L"Scintilla", NULL);
        if (!hwnd_edit)
            return;
        SendMessageW(hwnd_edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(str.c_str()));
    }

    void flush_() override
    {

    }
};

using notepad3_sink_mt = notepad3_sink<std::mutex>;

void enable_notepad_sink()
{
    spdlog::default_logger()->sinks().push_back(std::make_shared<notepad3_sink_mt>());
}

}  // namespace vNerve::bilibili::util
