#pragma once

#include <cstdint>

// ARM PrimeCell PL011 UART 的接收端。輸出端是各平台自己的主控台後端，
// 這裡只負責把收到的字元變成中斷驅動的主控台輸入。
//
// The receive side of an ARM PrimeCell PL011 UART. The transmit side is each
// platform's own console backend; this file only turns received characters
// into interrupt-driven console input.
namespace shirley::platform::arm {

// QEMU virt 把 UART0 接在 SPI 1，也就是 GIC 的中斷編號 33。
// QEMU virt wires UART0 to SPI 1, which is GIC interrupt number 33.
constexpr unsigned pl011_virt_irq = 33;

// 開啟接收中斷並註冊處理常式；base 為 0 或註冊失敗時回傳 false。
// Enable the receive interrupt and register the handler; returns false when
// base is zero or registration fails.
bool pl011_input_initialize(std::uintptr_t base, unsigned irq);
// 自開機以來從 UART 收到的字元數，供診斷使用。
// Characters received from the UART since boot, for diagnostics.
std::uint64_t pl011_input_characters();

} // namespace shirley::platform::arm
