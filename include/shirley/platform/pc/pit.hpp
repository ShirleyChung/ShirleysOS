#pragma once

#include <cstdint>

// 傳統 PC 的 8253/8254 可程式化間隔計時器，接在 IRQ0 上。
// The legacy PC 8253/8254 programmable interval timer, wired to IRQ0.
namespace shirley::platform::pc {

// 計時器固定接在 IRQ0。
// The timer is always on IRQ0.
constexpr unsigned pit_irq = 0;
// 晶片的輸入頻率固定為 1.193182 MHz，所有分頻值都由它算出。
// The chip's input frequency is fixed at 1.193182 MHz; every divisor is
// derived from it.
constexpr std::uint32_t pit_base_frequency = 1193182;
// 開機階段使用的中斷頻率；100 Hz 代表每個 tick 10 毫秒。
// The interrupt rate used during bring-up; 100 Hz makes one tick 10 ms.
constexpr unsigned pit_default_frequency = 100;

// 設定通道 0 為週期性中斷並註冊 IRQ0 處理常式；成功時回傳 true。
// Program channel 0 for periodic interrupts and register the IRQ0 handler;
// returns true on success.
bool pit_initialize(unsigned frequency);
// 自開機以來的計時器中斷次數。
// Timer interrupts counted since boot.
std::uint64_t pit_ticks();
// 實際設定的中斷頻率；分頻值是整數，因此可能與要求的頻率略有差異。
// The interrupt rate actually programmed. The divisor is an integer, so it can
// differ slightly from the requested rate.
unsigned pit_frequency();

} // namespace shirley::platform::pc
