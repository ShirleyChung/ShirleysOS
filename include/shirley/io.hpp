#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley::io {

enum class Error : std::uint8_t { None, Unsupported, InvalidArgument, OutOfRange, DeviceError };

struct Result {
    std::size_t transferred = 0;
    Error error = Error::None;
    explicit operator bool() const { return error == Error::None; }
};

class ByteStream {
public:
    virtual Result read(void* buffer, std::size_t length) = 0;
    virtual Result write(const void* buffer, std::size_t length) = 0;
protected:
    ~ByteStream() = default;
};

void set_standard_input(ByteStream* stream);
void set_standard_output(ByteStream* stream);
void set_standard_error(ByteStream* stream);
ByteStream* standard_input();
ByteStream* standard_output();
ByteStream* standard_error();
Result read_standard_input(void* buffer, std::size_t length);
Result write_standard_output(const void* buffer, std::size_t length);
Result write_standard_error(const void* buffer, std::size_t length);
void initialize_console_streams();

} // namespace shirley::io
