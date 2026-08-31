#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley::io {

// I/O 操作的失敗原因。
// Why an I/O operation failed.
enum class Error : std::uint8_t { None, Unsupported, InvalidArgument, OutOfRange, DeviceError };

// 實際傳輸的位元組數與錯誤碼；成功時可直接當成布林值使用。
// How many bytes moved plus an error code; converts to true on success.
struct Result {
    std::size_t transferred = 0;
    Error error = Error::None;
    explicit operator bool() const { return error == Error::None; }
};

// 位元組串流介面，主控台與未來的檔案都以此為抽象。
// The byte stream interface, used by the console today and files later.
class ByteStream {
public:
    virtual Result read(void* buffer, std::size_t length) = 0;
    virtual Result write(const void* buffer, std::size_t length) = 0;
protected:
    ~ByteStream() = default;
};

// 標準輸入、輸出與錯誤串流的設定與查詢。
// Set and query the standard input, output, and error streams.
void set_standard_input(ByteStream* stream);
void set_standard_output(ByteStream* stream);
void set_standard_error(ByteStream* stream);
ByteStream* standard_input();
ByteStream* standard_output();
ByteStream* standard_error();
// 對標準串流的便利存取；串流未設定時回傳 Unsupported。
// Convenience access to the standard streams; returns Unsupported when a
// stream has not been set.
Result read_standard_input(void* buffer, std::size_t length);
Result write_standard_output(const void* buffer, std::size_t length);
Result write_standard_error(const void* buffer, std::size_t length);
// 將標準輸出與標準錯誤接到平台主控台。標準輸入不在這裡設定：要等第一個
// 輸入裝置接上主控台，console::attach_input() 才會把它指過去。
//
// Point standard output and standard error at the platform console. Standard
// input is not set here: console::attach_input() points it at the console once
// the first input device attaches.
void initialize_console_streams();

} // namespace shirley::io
