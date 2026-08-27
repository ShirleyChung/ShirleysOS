#include "shirley/console.hpp"

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
