#include "shirley/console.hpp"

#include "shirley/device.hpp"

#ifdef SHIRLEY_HOST
#include <cstdio>
#endif

namespace shirley::console {
Backend* active_backend = nullptr;

namespace {

#ifdef SHIRLEY_HOST
class ShellConsole final : public Backend {
public:
    void initialize() override {}
    void write(const char* text, std::size_t length) override {
        if (text != nullptr) std::fwrite(text, 1, length, stdout);
    }
};

ShellConsole shell_console;
#endif
} // namespace

#ifdef SHIRLEY_HOST
Backend* default_backend() { return &shell_console; }
#endif

void set_backend(Backend* value) { active_backend = value; }

Backend* backend() { return active_backend != nullptr ? active_backend : default_backend(); }

void initialize() {
    Backend* selected = backend();
    if (selected != nullptr) selected->initialize();
    // 後端可以輸出之後，主控台自己也成為註冊表裡的一個裝置。輸入裝置這時
    // 通常還不存在——驅動程式要等平台初始化才會上線——但主控台的輸出從這一
    // 刻起就是可用的，而 /dev/console 指的正是這條路徑。
    //
    // Once the backend can print, the console itself becomes a device in the
    // registry. Input devices usually do not exist yet, since their drivers
    // come up during platform initialization, but console output works from
    // this moment on, and that path is exactly what /dev/console names.
    device::register_device(console_device());
}

void write(const char* text) {
    if (text == nullptr) return;
    std::size_t length = 0;
    while (text[length] != '\0') ++length;
    write(text, length);
}

void write(const char* text, std::size_t length) {
    if (text == nullptr && length != 0) return;
    Backend* selected = backend();
    if (selected != nullptr) selected->write(text, length);
}
} // namespace shirley::console
