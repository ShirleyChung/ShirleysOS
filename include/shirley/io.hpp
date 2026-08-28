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
// 將標準輸出與標準錯誤接到平台主控台。
// Point standard output and standard error at the platform console.
void initialize_console_streams();

// 所有輸入裝置共用的字元佇列。一台機器可能同時有鍵盤與序列埠，兩者都應該
// 能驅動同一個 shell，因此驅動程式把解碼後的字元推進這裡，而不是各自維護
// 一條輸入路徑。回顯不在這一層：那是行編輯器的職責。
//
// The character queue every input device shares. A machine can have both a
// keyboard and a serial port, and either should be able to drive the same
// shell, so a driver pushes its decoded characters here instead of keeping an
// input path of its own. Echo does not belong at this layer; it is the line
// editor's job.
class InputQueue;
InputQueue& console_input();
// 由中斷處理常式呼叫；佇列已滿時捨棄字元並回傳 false。
// Called from an interrupt handler; a full queue drops the character and
// returns false.
bool console_input_push(char value);
// 把標準輸入接到共用佇列。輸入裝置驅動程式初始化成功後呼叫，因為在那之前
// 沒有任何東西會把字元推進來，標準輸入應該維持未設定。
//
// Point standard input at the shared queue. An input driver calls this once it
// is up, because until then nothing pushes characters and standard input
// should stay unset.
void attach_console_input();

} // namespace shirley::io
