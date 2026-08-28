#pragma once

#include <cstdint>

namespace shirley::platform::apple {

// Apple SoC 的除錯 UART 基底位址隨機型不同，開機時由平台層決定。
// The debug UART base address differs per Apple SoC, so the platform layer
// selects it at boot.
void console_set_uart_base(std::uintptr_t base);
// 目前使用中的 UART 位址；平台層列出必須保持映射的裝置記憶體時會用到。
// The UART address currently in use, needed when the platform layer lists the
// device memory that has to stay mapped.
std::uintptr_t console_uart_base();

// AIC（Apple Interrupt Controller）的基底位址同樣隨機型不同。初始化會一併
// 把分辨來源的處理常式掛上 IRQ 例外入口，失敗時回傳 false。
//
// The AIC (Apple Interrupt Controller) base address is likewise per-SoC.
// Initialization also hooks the source-identifying handler onto the IRQ
// exception entry, and returns false when that fails.
bool interrupt_controller_initialize(std::uintptr_t base);
void interrupt_controller_mask(unsigned irq);
void interrupt_controller_unmask(unsigned irq);
void interrupt_controller_end_of_interrupt(unsigned irq);
bool interrupt_controller_present();

} // namespace shirley::platform::apple
