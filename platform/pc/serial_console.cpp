#include "shirley/console.hpp"

#include "shirley/arch/x86_64/port_io.hpp"

// 每台 IBM PC 相容機器都有 COM1，不論韌體是 BIOS 還是 UEFI，
// 因此這個主控台由所有 PC 平台共用。
// Every IBM-PC-compatible machine has COM1 regardless of whether its firmware
// is a BIOS or UEFI, so this console is shared by all PC platforms.
namespace shirley::console {
namespace {

using arch::x86_64::inb;
using arch::x86_64::outb;

// 傳統 PC 的 COM1 序列埠暫存器。
// The COM1 serial port registers on a legacy PC.
constexpr std::uint16_t com1 = 0x3f8;
constexpr std::uint16_t interrupt_enable = com1 + 1;
constexpr std::uint16_t fifo_control = com1 + 2;
constexpr std::uint16_t line_control = com1 + 3;
constexpr std::uint16_t modem_control = com1 + 4;
constexpr std::uint16_t line_status = com1 + 5;
// 除數閂鎖存取位元，用來設定鮑率。
// The divisor latch access bit, used to set the baud rate.
constexpr std::uint8_t divisor_latch = 0x80;
// 8 個資料位元、無同位、1 個停止位元。
// Eight data bits, no parity, one stop bit.
constexpr std::uint8_t eight_bits_no_parity = 0x03;
constexpr std::uint8_t transmitter_empty = 1u << 5;

// 等待傳送保持暫存器清空後再送出下一個位元組。
// Wait for the transmit holding register to drain before sending another byte.
void put(char value) {
    while ((inb(line_status) & transmitter_empty) == 0) {}
    outb(com1, static_cast<std::uint8_t>(value));
}

} // namespace

// 設定 COM1 的鮑率、資料格式與 FIFO。
// Configure COM1's baud rate, framing, and FIFOs.
void initialize() {
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

// 將字元送到 COM1；換行時先送回車符。
// Send characters to COM1, prefixing a carriage return before each newline.
void write(const char* text, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') put('\r');
        put(text[i]);
    }
}

// 計算 null 結尾字串長度後輸出。
// Measure a null-terminated string, then write it.
void write(const char* text) {
    if (text == nullptr) return;
    std::size_t length = 0;
    while (text[length] != '\0') ++length;
    write(text, length);
}

} // namespace shirley::console
