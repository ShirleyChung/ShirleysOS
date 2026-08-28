#pragma once

#include <cstdint>

// 傳統 PC 的 COM1 序列埠。輸出端是主控台後端（platform/pc/serial_console.cpp），
// 輸入端是中斷驅動的接收器（platform/pc/serial_input.cpp）；兩者共用同一組
// 暫存器位址，因此位址定義放在這裡而不是各自複製一份。
//
// The legacy PC COM1 serial port. The output side is a console backend
// (platform/pc/serial_console.cpp) and the input side is an interrupt-driven
// receiver (platform/pc/serial_input.cpp). Both address the same registers, so
// the addresses are defined here rather than copied into each file.
namespace shirley::platform::pc {

// COM1 的基底位址與各暫存器的位移。
// COM1's base address and its register offsets.
constexpr std::uint16_t com1_base = 0x3f8;
constexpr std::uint16_t com1_data = com1_base;
constexpr std::uint16_t com1_interrupt_enable = com1_base + 1;
constexpr std::uint16_t com1_interrupt_identify = com1_base + 2;
constexpr std::uint16_t com1_fifo_control = com1_base + 2;
constexpr std::uint16_t com1_line_control = com1_base + 3;
constexpr std::uint16_t com1_modem_control = com1_base + 4;
constexpr std::uint16_t com1_line_status = com1_base + 5;

// 線路狀態位元 0：有收到的位元組可讀。位元 5：傳送保持暫存器已清空。
// Line status bit 0: a received byte is waiting. Bit 5: the transmit holding
// register is empty.
constexpr std::uint8_t com1_data_ready = 1u << 0;
constexpr std::uint8_t com1_transmitter_empty = 1u << 5;
// 中斷致能位元 0：收到資料時產生中斷。
// Interrupt enable bit 0: raise an interrupt when data arrives.
constexpr std::uint8_t com1_receive_interrupt = 1u << 0;
// FIFO 控制：啟用並清空收發 FIFO，觸發門檻設為 1 個位元組。終端機一次只送
// 一個字元，門檻設高的話按鍵會留在 FIFO 裡等不到中斷。
//
// FIFO control: enable and clear both FIFOs with a one-byte trigger level. A
// terminal sends one character at a time, and a higher threshold leaves
// keystrokes sitting in the FIFO waiting for an interrupt that never comes.
constexpr std::uint8_t com1_fifo_enable_single_byte = 0x07;
// 中斷要求 OUT2 接通才會真的送到中斷控制器，這是 PC 接線方式的一部分。
// An interrupt only reaches the controller when OUT2 is asserted; that is how
// a PC is wired.
constexpr std::uint8_t com1_modem_ready = 0x03;
constexpr std::uint8_t com1_modem_out2 = 1u << 3;

// COM1 與 COM3 共用 IRQ4。
// COM1 shares IRQ4 with COM3.
constexpr unsigned serial_irq = 4;

// 啟用 COM1 的接收中斷並註冊 IRQ4 處理常式，讓序列埠終端機也能當成主控台
// 輸入。成功時回傳 true。
//
// Enable COM1's receive interrupt and register the IRQ4 handler so a serial
// terminal can drive console input too. Returns true on success.
bool serial_input_initialize();
// 自開機以來從序列埠收到的字元數，供診斷使用。
// Characters received from the serial port since boot, for diagnostics.
std::uint64_t serial_input_characters();

} // namespace shirley::platform::pc
