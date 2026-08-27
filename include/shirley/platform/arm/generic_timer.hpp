#pragma once

#include <cstdint>

// ARM 架構計時器。它是 CPU 自帶的週邊，透過系統暫存器操作，因此每一台
// ARM64 機器都有，不必再為每個平台各寫一顆計時器驅動程式。
//
// The ARM architected timer. It is a peripheral the CPU carries with it,
// driven through system registers, so every ARM64 machine has one and no
// per-platform timer driver is needed.
namespace shirley::platform::arm {

// 非安全 EL1 實體計時器固定使用 PPI 30；PPI 是每個核心私有的中斷。
// The non-secure EL1 physical timer is always PPI 30, and a PPI is an
// interrupt private to one core.
constexpr unsigned generic_timer_irq = 30;
// 開機階段使用的中斷頻率，與 PC 的 PIT 一致，每個 tick 10 毫秒。
// The interrupt rate used during bring-up, matching the PC's PIT at one tick
// every 10 ms.
constexpr unsigned generic_timer_default_frequency = 100;

// 設定週期性中斷並註冊 PPI 30 處理常式；CPU 沒有回報計時器頻率時回傳 false。
// Program periodic interrupts and register the PPI 30 handler; returns false
// when the CPU reports no timer frequency.
bool generic_timer_initialize(unsigned frequency);
std::uint64_t generic_timer_ticks();
// 實際設定的中斷頻率；間隔是整數個計數，因此可能與要求的頻率略有差異。
// The interrupt rate actually programmed. The interval is a whole number of
// counts, so it can differ slightly from the requested rate.
unsigned generic_timer_frequency();

} // namespace shirley::platform::arm
