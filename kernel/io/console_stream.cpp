#include "shirley/console.hpp"
#include "shirley/io.hpp"

namespace shirley::io {
namespace {
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
    set_standard_input(nullptr);
    set_standard_output(&console_stream);
    set_standard_error(&console_stream);
}
} // namespace shirley::io
