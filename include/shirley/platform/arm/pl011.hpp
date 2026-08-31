#pragma once

#include "shirley/device.hpp"

#include <cstdint>

// ARM PrimeCell PL011 UART 的接收端，以及它的裝置包裝 uart0。平台的主控台
// 輸出仍然走各平台自己的後端，這裡負責把收到的字元變成中斷驅動的主控台輸入，
// 並讓同一顆 UART 以 uart0 的名字被讀寫。
//
// The receive side of an ARM PrimeCell PL011 UART, and its uart0 device
// wrapper. Console output still goes through each platform's own backend;
// this file turns received characters into interrupt-driven console input and
// makes the same UART readable and writable under the name uart0.
namespace shirley::platform::arm {

// QEMU virt 把 UART0 接在 SPI 1，也就是 GIC 的中斷編號 33。
// QEMU virt wires UART0 to SPI 1, which is GIC interrupt number 33.
constexpr unsigned pl011_virt_irq = 33;

// 開啟接收中斷、註冊處理常式、把這顆 UART 登記成 uart0 並接上主控台輸入；
// base 為 0 或任一步失敗時回傳 false。
//
// Enable the receive interrupt, register the handler, publish the UART as
// uart0, and attach it to console input; returns false when base is zero or
// any step fails.
bool pl011_input_initialize(std::uintptr_t base, unsigned irq);
// UART 的裝置物件，等同於 device::find("uart0")。
// The UART's device object, the same thing device::find("uart0") returns.
device::Device* pl011_device();
// 自開機以來從 UART 收到的字元數，供診斷使用。
// Characters received from the UART since boot, for diagnostics.
std::uint64_t pl011_input_characters();

} // namespace shirley::platform::arm
