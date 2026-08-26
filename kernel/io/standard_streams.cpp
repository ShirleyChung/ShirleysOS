#include "shirley/io.hpp"

namespace shirley::io {
namespace {
ByteStream* input = nullptr;
ByteStream* output = nullptr;
ByteStream* error = nullptr;

// 共用的參數檢查：空緩衝區是參數錯誤，未設定串流是不支援。
// Shared argument checking: a null buffer is an argument error, while an
// unset stream is simply unsupported.
Result read(ByteStream* stream, void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    if (stream == nullptr) return {0, Error::Unsupported};
    return stream->read(buffer, length);
}

Result write(ByteStream* stream, const void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    if (stream == nullptr) return {0, Error::Unsupported};
    return stream->write(buffer, length);
}
} // namespace

void set_standard_input(ByteStream* stream) { input = stream; }
void set_standard_output(ByteStream* stream) { output = stream; }
void set_standard_error(ByteStream* stream) { error = stream; }
ByteStream* standard_input() { return input; }
ByteStream* standard_output() { return output; }
ByteStream* standard_error() { return error; }
Result read_standard_input(void* buffer, std::size_t length) { return read(input, buffer, length); }
Result write_standard_output(const void* buffer, std::size_t length) { return write(output, buffer, length); }
Result write_standard_error(const void* buffer, std::size_t length) { return write(error, buffer, length); }
} // namespace shirley::io
