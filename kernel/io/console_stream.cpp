#include "shirley/console.hpp"
#include "shirley/io.hpp"

namespace shirley::io {
namespace {
// 把平台主控台包成位元組串流；主控台目前只能輸出。
// Wraps the platform console as a byte stream. The console is output-only.
class ConsoleStream final : public ByteStream {
public:
    Result read(void*, std::size_t) override { return {0, Error::Unsupported}; }
    Result write(const void* buffer, std::size_t length) override {
        if (length != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
        console::write(static_cast<const char*>(buffer), length);
        return {length, Error::None};
    }
};
ConsoleStream console_stream;
} // namespace

void initialize_console_streams() {
    // 標準輸入先留空：這時還沒有任何輸入裝置，指向一個永遠讀不到東西的
    // 串流只會讓 shell 以為機器可以打字。有輸入裝置的平台會在驅動程式初始化
    // 成功時呼叫 console::attach_input()，標準輸入才在那一刻接上主控台。
    //
    // Standard input starts unset: no input device exists yet, and pointing it
    // at a stream that can never produce a character would only make the shell
    // believe the machine can be typed at. A platform with an input device
    // calls console::attach_input() when that driver comes up, and standard
    // input is connected to the console at that moment.
    set_standard_input(nullptr);
    set_standard_output(&console_stream);
    set_standard_error(&console_stream);
}
} // namespace shirley::io
