#pragma once

#include <cstdint>

namespace shirley::platform::apple {

// Apple SoC 的除錯 UART 基底位址隨機型不同，開機時由平台層決定。
// The debug UART base address differs per Apple SoC, so the platform layer
// selects it at boot.
void console_set_uart_base(std::uintptr_t base);

// AIC（Apple Interrupt Controller）的基底位址同樣隨機型不同。
// The AIC (Apple Interrupt Controller) base address is likewise per-SoC.
void interrupt_controller_initialize(std::uintptr_t base);
void interrupt_controller_mask(unsigned irq);
void interrupt_controller_unmask(unsigned irq);
void interrupt_controller_end_of_interrupt(unsigned irq);
bool interrupt_controller_present();

} // namespace shirley::platform::apple
