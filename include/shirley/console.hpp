#pragma once

#include "shirley/device.hpp"
#include "shirley/io.hpp"

#include <cstddef>

// 主控台層。上層（printf、行編輯器、shell）只認得這裡的介面，不知道字元最後
// 是送到 UART、framebuffer 還是主機的 stdout，也不知道按鍵是從 PS/2 鍵盤還是
// 序列線進來的：
//
//     shell / readline / printf
//                ↓
//             console
//           ↙         ↘
//        kbd0        uart0 / framebuffer
//
// 輸出走可替換的 Backend，輸入走已註冊的裝置。兩邊不對稱是因為它們的問題
// 不一樣：輸出只有一個目的地，輸入則可能同時有好幾個來源。
//
// The console layer. Everything above it — printf, the line editor, the shell
// — knows only this interface, and never whether characters end up on a UART,
// a framebuffer, or the host's standard output, nor whether keystrokes come
// from a PS/2 keyboard or down a serial line:
//
//     shell / readline / printf
//                ↓
//             console
//           ↙         ↘
//        kbd0        uart0 / framebuffer
//
// Output goes through a replaceable Backend and input through registered
// devices. The asymmetry is deliberate: output has one destination, while
// input can have several sources at once.
namespace shirley::console {

// 可替換的主控台輸出後端。核心只依賴這個小介面，平台或測試可以在
// initialize() 前換成自己的實作。
// A replaceable console backend. The kernel depends only on this small
// interface; a platform or test may install its own implementation before
// initialize().
class Backend {
public:
    virtual void initialize() = 0;
    virtual void write(const char* text, std::size_t length) = 0;

protected:
    ~Backend() = default;
};

// 設定目前後端；傳入 nullptr 會恢復平台預設後端。
// Select the active backend; nullptr restores the platform default.
void set_backend(Backend* backend);
Backend* backend();

// 每個平台提供自己的預設後端。host 平台的預設值是 shell console。
// Each platform supplies its default backend. The host default is the shell
// console.
Backend* default_backend();

// 初始化目前平台的主控台輸出裝置，並把主控台自己登記成一個裝置。
// Initialize the current platform's console output device and publish the
// console itself as a device.
void initialize();
// 輸出以 null 結尾的字串。
// Write a null-terminated string.
void write(const char* text);
// 輸出指定長度的位元組序列。
// Write a byte sequence of the given length.
void write(const char* text, std::size_t length);

// 主控台可以同時有幾個輸入裝置。一台 PC 上鍵盤與序列埠終端機都應該能驅動
// 同一個 shell，因此輸入是一組來源而不是一個。
//
// How many input devices the console can have at once. On a PC both the
// keyboard and a serial terminal should be able to drive the same shell, so
// input is a set of sources rather than a single one.
constexpr std::size_t max_input_devices = 4;

// 把輸入裝置接上主控台。驅動程式初始化成功後自己呼叫，因為在那之前沒有任何
// 東西會送出字元。第一個裝置接上時，標準輸入也會跟著指向主控台輸入。
//
// 裝置為輸入來源時只需要提供 read；主控台不會對它寫入。
//
// Attach an input device to the console. A driver calls this once it is up,
// because until then nothing produces characters. Attaching the first device
// also points standard input at the console's input.
//
// An input source needs only read; the console never writes to it.
bool attach_input(device::Device& device);
bool detach_input(device::Device& device);
std::size_t input_count();
device::Device* input_device(std::size_t index);

// 依序向每個輸入裝置取字元，回傳第一個真的有資料的結果。不會等待：沒有任何
// 裝置有字元時傳輸 0 個位元組，呼叫端自行決定要等中斷還是做別的事。
//
// 這個函式會在中斷開啟的情況下由一般核心程式呼叫，中斷處理常式則同時在另一
// 端把字元推進裝置的環狀緩衝區。安全性來自那個緩衝區本身是單生產者、單消費者
// 的免鎖佇列，因此這裡不需要關中斷，也不會在中斷情境裡取任何鎖。
//
// Take characters from each input device in turn and return the first result
// that actually carries data. It never waits: with nothing queued anywhere it
// transfers zero bytes and the caller decides whether to wait for an interrupt
// or do something else.
//
// Ordinary kernel code calls this with interrupts enabled while an interrupt
// handler pushes into the very same device's ring buffer. What makes that safe
// is the buffer being a lock-free single-producer, single-consumer queue, so
// nothing here disables interrupts and no lock is ever taken in interrupt
// context.
io::Result read(void* buffer, std::size_t length);

// 主控台本身作為一個裝置：寫入送到後端，讀取來自輸入裝置。未來的 devfs 把
// 它掛在 /dev/console，不需要再寫一次轉接。
//
// The console as a device: writes reach the backend and reads come from the
// input devices. A future devfs mounts it at /dev/console without another
// adapter being written.
device::Device& console_device();
// 主控台輸入的位元組串流形式，標準輸入指向的就是它。
// The console's input as a byte stream, which is what standard input points
// at.
io::ByteStream& input_stream();

} // namespace shirley::console
