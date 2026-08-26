#include "shirley/console.hpp"

namespace shirley::console {
namespace {

// QEMU virt 平台上的 PL011 UART 基底位址。
// The PL011 UART base address on the QEMU virt machine.
volatile unsigned int* const uart = reinterpret_cast<volatile unsigned int*>(0x09000000);
// PL011 的暫存器以 32 位元字為單位編號。
// PL011 registers are indexed as 32-bit words.
constexpr unsigned data = 0;
constexpr unsigned flag = 6;
constexpr unsigned line_control = 11;
constexpr unsigned control = 12;
// FR 的位元 5 表示傳送 FIFO 已滿。
// Bit 5 of the flag register means the transmit FIFO is full.
constexpr unsigned transmit_full = 1u << 5;
// LCR_H：8 個資料位元。 / LCR_H: eight data bits.
constexpr unsigned eight_bits = 3u << 5;
// CR：啟用 UART、傳送與接收。 / CR: enable the UART, transmit, and receive.
constexpr unsigned enable = (1u << 0) | (1u << 8) | (1u << 9);

// 每送一個位元組前都確認 FIFO 還有空間。
// Check for room in the FIFO before every byte.
void put(char value) {
    while (uart[flag] & transmit_full) {}
    uart[data] = static_cast<unsigned char>(value);
}

} // namespace

// 設定 UART 為可傳送字元的基本模式。
// Put the UART into the minimal state needed to send characters.
void initialize() {
    uart[control] = 0;
    uart[line_control] = eight_bits;
    uart[control] = enable;
}

// 等待 UART 有空間後逐字輸出；換行時補上回車符。
// Wait for room in the FIFO, then send each character, prefixing a carriage
// return before each newline.
void write(const char* text, std::size_t length) {
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') put('\r');
        put(text[i]);
    }
}

// 計算字串長度後交給長度版本輸出。
// Measure the string, then hand it to the length-taking overload.
void write(const char* text) {
    if (text == nullptr) return;
    std::size_t length = 0;
    while (text[length] != '\0') ++length;
    write(text, length);
}

} // namespace shirley::console
