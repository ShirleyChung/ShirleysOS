#pragma once

#include <cstdint>

// 傳統 PC 的 PS/2 鍵盤，接在 8042 控制器的第一個連接埠與 IRQ1 上。
// The legacy PC PS/2 keyboard, on the 8042 controller's first port and IRQ1.
namespace shirley::platform::pc {

// 鍵盤固定接在 IRQ1。
// The keyboard is always on IRQ1.
constexpr unsigned ps2_keyboard_irq = 1;

// 初始化 8042 控制器、註冊 IRQ1 處理常式，並把鍵盤佇列接成標準輸入。
// 成功時回傳 true。
//
// Initialize the 8042 controller, register the IRQ1 handler, and attach the
// keyboard queue as standard input. Returns true on success.
bool ps2_keyboard_initialize();

// 目前已排隊、尚未被讀走的字元數。
// How many characters are queued and not yet read.
std::uint64_t ps2_keyboard_pending();
// 自開機以來解出的字元總數，供診斷使用。
// Characters decoded since boot, for diagnostics.
std::uint64_t ps2_keyboard_characters();

} // namespace shirley::platform::pc
