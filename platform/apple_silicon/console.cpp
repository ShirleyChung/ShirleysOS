#include "shirley/console.hpp"

#include "internal.hpp"

namespace shirley::platform::apple {
std::uintptr_t uart_base = 0x235200000ull;

namespace {

// Apple Silicon 的除錯序列埠是 Samsung S5L 系列 UART 的衍生版本，
// 與 QEMU virt 的 PL011 完全不同，因此不能共用 platform/qemu_arm64 的驅動程式。
// 基底位址隨 SoC 不同，開機時由 boot_args 的 device tree 決定；
// 在取得該資訊之前使用 M1（t8103）的 UART0 位址。
//
// Apple Silicon's debug serial port is derived from the Samsung S5L UART and
// is nothing like the PL011 on QEMU virt, so the platform/qemu_arm64 driver
// cannot be reused. The base address is per-SoC and normally comes from the
// device tree in boot_args; until that is parsed, the M1 (t8103) UART0 address
// is used.
constexpr std::uintptr_t default_uart_base = 0x235200000ull;

// S5L UART 的暫存器位移。
// Register offsets within the S5L UART.
constexpr std::uintptr_t line_control = 0x00;
constexpr std::uintptr_t control = 0x04;
constexpr std::uintptr_t fifo_control = 0x08;
constexpr std::uintptr_t status = 0x10;
constexpr std::uintptr_t transmit = 0x20;
// UTRSTAT 的位元 2 表示傳送器已經清空。
// Bit 2 of UTRSTAT means the transmitter has drained.
constexpr std::uint32_t transmitter_empty = 1u << 2;

volatile std::uint32_t* uart_register(std::uintptr_t offset) {
    return reinterpret_cast<volatile std::uint32_t*>(uart_base + offset);
}

void put(char value) {
    while ((*uart_register(status) & transmitter_empty) == 0) {}
    *uart_register(transmit) = static_cast<std::uint8_t>(value);
}

} // namespace

// iBoot 已經設定好鮑率，這裡只確認 8N1 格式並開啟收發器。
// iBoot has already set the baud rate, so this only confirms 8N1 framing and
// enables the transmitter and receiver.
class AppleUartConsole final : public console::Backend {
public:
void initialize() override {
    *uart_register(line_control) = 0x03;
    *uart_register(fifo_control) = 0x00;
    *uart_register(control) = 0x05;
}

// 送出字元；換行時先送回車符。
// Send characters, prefixing a carriage return before each newline.
void write(const char* text, std::size_t length) override {
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') put('\r');
        put(text[i]);
    }
}

// 計算 null 結尾字串長度後輸出。
// Measure a null-terminated string, then write it.
};

AppleUartConsole apple_uart_console;

} // namespace shirley::platform::apple

namespace shirley::console {
Backend* default_backend() { return &platform::apple::apple_uart_console; }
} // namespace shirley::console

namespace shirley::platform::apple {

// 讓平台層在解析完韌體資料後改用正確的 UART 位址。
// Lets the platform layer switch to the real UART address once it has parsed
// the firmware data.
void console_set_uart_base(std::uintptr_t base) {
    if (base != 0) uart_base = base;
}

} // namespace shirley::platform::apple
