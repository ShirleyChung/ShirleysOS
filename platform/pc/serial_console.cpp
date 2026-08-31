#include "shirley/console.hpp"

#include "shirley/arch/x86_64/port_io.hpp"
#include "shirley/platform/pc/serial.hpp"

// 每台 IBM PC 相容機器都有 COM1，不論韌體是 BIOS 還是 UEFI，
// 因此這個主控台由所有 PC 平台共用。
// Every IBM-PC-compatible machine has COM1 regardless of whether its firmware
// is a BIOS or UEFI, so this console is shared by all PC platforms.
namespace shirley::platform::pc {
namespace {

using arch::x86_64::inb;
using arch::x86_64::outb;

// 暫存器位址與狀態位元由 shirley/platform/pc/serial.hpp 提供，輸入端的驅動
// 程式也用同一份定義。
// The register addresses and status bits come from
// shirley/platform/pc/serial.hpp, which the input side uses as well.
constexpr std::uint16_t com1 = com1_data;
constexpr std::uint16_t interrupt_enable = com1_interrupt_enable;
constexpr std::uint16_t fifo_control = com1_fifo_control;
constexpr std::uint16_t line_control = com1_line_control;
constexpr std::uint16_t modem_control = com1_modem_control;
constexpr std::uint16_t line_status = com1_line_status;
// 除數閂鎖存取位元，用來設定鮑率。
// The divisor latch access bit, used to set the baud rate.
constexpr std::uint8_t divisor_latch = 0x80;
// 8 個資料位元、無同位、1 個停止位元。
// Eight data bits, no parity, one stop bit.
constexpr std::uint8_t eight_bits_no_parity = 0x03;
constexpr std::uint8_t transmitter_empty = com1_transmitter_empty;

// 等待傳送保持暫存器清空後再送出下一個位元組。
// Wait for the transmit holding register to drain before sending another byte.
void put(char value) {
    while ((inb(line_status) & transmitter_empty) == 0) {}
    outb(com1, static_cast<std::uint8_t>(value));
}

} // namespace

// 設定 COM1 的鮑率、資料格式與 FIFO。
// Configure COM1's baud rate, framing, and FIFOs.
class SerialConsole final : public console::Backend {
public:
void initialize() override {
    outb(interrupt_enable, 0x00);
    outb(line_control, divisor_latch);
    // 除數 3 對應 38400 鮑率。
    // A divisor of 3 gives 38400 baud.
    outb(com1, 0x03);
    outb(interrupt_enable, 0x00);
    outb(line_control, eight_bits_no_parity);
    // 啟用並清空收發 FIFO，觸發門檻設為 14 位元組。
    // Enable and clear both FIFOs with a 14-byte trigger level.
    outb(fifo_control, 0xc7);
    // 設定 DTR 與 RTS。
    // Assert DTR and RTS.
    outb(modem_control, 0x03);
}

// 將字元送到 COM1。
// Send characters to COM1.
void write(const char* text, std::size_t length) override { serial_write(text, length); }
};

SerialConsole serial_console;

} // namespace shirley::platform::pc

namespace shirley::platform::pc {

// 主控台後端與 uart0 裝置的共同傳送路徑：換行時先送回車符，終端機才會回到
// 行首。兩者共用同一個函式，是為了讓 /dev/uart0 寫出來的東西和主控台印出來
// 的東西在線路上完全一樣。
//
// The transmit path shared by the console backend and the uart0 device: a
// newline is prefixed with a carriage return so a terminal returns to the
// start of the line. Both go through one function so what /dev/uart0 writes
// and what the console prints are identical on the wire.
void serial_write(const char* text, std::size_t length) {
    if (text == nullptr) return;
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') put('\r');
        put(text[i]);
    }
}

} // namespace shirley::platform::pc

namespace shirley::console {
Backend* default_backend() { return &platform::pc::serial_console; }
} // namespace shirley::console
