#pragma once

#include "shirley/boot_info.hpp"

namespace shirley::platform {

// 平台層的裝置中斷編號，與架構的中斷向量編號分開。
// A platform device interrupt number, kept separate from architecture vectors.
using Irq = unsigned;

// 平台提供的能力；通用核心據此決定行為，而不是判斷機器型號。
// What the platform provides. Generic code branches on these instead of
// testing for a particular machine by name.
struct Capabilities {
    bool serial_console = false;
    bool interrupt_controller = false;
    bool timer = false;
    bool framebuffer = false;
};

// 初始化平台層；必須在 arch::initialize() 之後呼叫。
// Initialize the platform layer; must run after arch::initialize().
void initialize(const BootInfo&);
// 平台識別名稱，以及較詳細的機器描述。
// The platform identifier and a longer machine description.
const char* name();
const char* machine();
const Capabilities& capabilities();

// 中斷控制器操作；平台沒有中斷控制器時為無作用。
// Interrupt controller operations; no-ops on a platform without a controller.
void enable_irq(Irq);
void disable_irq(Irq);
void end_of_interrupt(Irq);
// 將平台 IRQ 對應到架構中斷向量。
// Map a platform IRQ onto an architecture interrupt vector.
unsigned irq_vector(Irq);

// 平台電源控制；韌體不支援時退回停機。
// Platform power control; falls back to halting when firmware has no support.
[[noreturn]] void power_off();
[[noreturn]] void restart();

} // namespace shirley::platform
