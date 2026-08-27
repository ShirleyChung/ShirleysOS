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
    // 主控台本身只能輸出，因此標準輸入先留空；有輸入裝置的平台會在自己的
    // 驅動程式初始化時把佇列接上來（PC 平台是 PS/2 鍵盤）。
    //
    // The console itself is output-only, so standard input starts unset. A
    // platform with an input device attaches its queue when that driver comes
    // up — the PS/2 keyboard on a PC.
    set_standard_input(nullptr);
    set_standard_output(&console_stream);
    set_standard_error(&console_stream);
}
} // namespace shirley::io
