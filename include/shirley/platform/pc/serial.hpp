#pragma once

#include "shirley/device.hpp"
#include "shirley/input_queue.hpp"

#include <cstddef>
#include <cstdint>

// 傳統 PC 的 COM1 序列埠，分成三個檔案：
//
//   platform/pc/serial_console.cpp  傳送端，同時是主控台後端
//   platform/pc/serial_input.cpp    IRQ4 接收端與它的環狀緩衝區
//   platform/pc/serial_device.cpp   把上面兩端包成 uart0 這個裝置
//
// 三者共用同一組暫存器位址，因此位址定義放在這裡而不是各自複製一份。裝置
// 包裝不改變任何底層 I/O 行為：uart0 的寫入走的就是主控台後端那條傳送路徑，
// 讀取取的就是 IRQ4 已經收進緩衝區的位元組。
//
// The legacy PC COM1 serial port, split across three files:
//
//   platform/pc/serial_console.cpp  the transmit side, and the console backend
//   platform/pc/serial_input.cpp    the IRQ4 receiver and its ring buffer
//   platform/pc/serial_device.cpp   both sides published as the uart0 device
//
// All three address the same registers, so the addresses are defined here
// rather than copied into each file. The device wrapper changes no underlying
// I/O behaviour: a write to uart0 takes the console backend's transmit path,
// and a read takes bytes IRQ4 has already placed in the buffer.
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

// 傳送端。主控台後端與 uart0 的寫入都走這裡，換行會補上回車符。
// The transmit side. Both the console backend and a write to uart0 come
// through here, and a newline is given its carriage return.
void serial_write(const char* text, std::size_t length);

// IRQ4 收到的位元組所在的環狀緩衝區，也就是 uart0 讀出來的內容。
// The ring buffer holding what IRQ4 received, which is what a read of uart0
// takes from.
io::InputQueue& serial_receive_queue();

// 把 COM1 登記成 uart0。可以在接收中斷還沒啟用時呼叫：即使這台機器沒有人接
// 終端機，序列埠仍然是可以寫入的裝置。
//
// Publish COM1 as uart0. It may be called before the receive interrupt is
// enabled: the serial port is a writable device even on a machine with no
// terminal attached to it.
bool serial_device_register();
// COM1 的裝置物件，等同於 device::find("uart0")。
// COM1's device object, the same thing device::find("uart0") returns.
device::Device* serial_device();

} // namespace shirley::platform::pc
