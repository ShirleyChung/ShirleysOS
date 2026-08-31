#pragma once

#include "shirley/device.hpp"

#include <cstdint>

// 傳統 PC 的 PS/2 鍵盤，接在 8042 控制器的第一個連接埠與 IRQ1 上。
//
//     PS/2 鍵盤 → IRQ1 → 中斷處理常式 → 環狀緩衝區 → kbd0 → console
//
// 中斷處理常式只做四件事：讀 0x60、解掃描碼、把字元推進環狀緩衝區、然後
// 返回；EOI 由既有的 IRQ/PIC 層負責。這條路徑上沒有 printf，也沒有 shell。
//
// The legacy PC PS/2 keyboard, on the 8042 controller's first port and IRQ1.
//
//     PS/2 keyboard → IRQ1 → handler → ring buffer → kbd0 → console
//
// The handler does four things: read port 0x60, decode the scancode, push the
// character into the ring buffer, and return. End-of-interrupt belongs to the
// existing IRQ/PIC layer. Nothing on this path prints, and nothing on it knows
// about the shell.
namespace shirley::platform::pc {

// 鍵盤固定接在 IRQ1。
// The keyboard is always on IRQ1.
constexpr unsigned ps2_keyboard_irq = 1;

// 初始化 8042 控制器、註冊 IRQ1 處理常式，把鍵盤登記成 kbd0，並接上主控台
// 輸入。成功時回傳 true。
//
// Initialize the 8042 controller, register the IRQ1 handler, publish the
// keyboard as kbd0, and attach it to console input. Returns true on success.
bool ps2_keyboard_initialize();

// 鍵盤的裝置物件。等同於 device::find("kbd0")，只是不需要查表；初始化失敗時
// 這個裝置不會出現在註冊表裡。
//
// The keyboard's device object. The same thing device::find("kbd0") returns,
// without the lookup; a failed initialization leaves it out of the registry.
device::Device* ps2_keyboard_device();

// 目前已排隊、尚未被讀走的字元數。
// How many characters are queued and not yet read.
std::uint64_t ps2_keyboard_pending();
// 自開機以來解出的字元總數，供診斷使用。
// Characters decoded since boot, for diagnostics.
std::uint64_t ps2_keyboard_characters();

} // namespace shirley::platform::pc
