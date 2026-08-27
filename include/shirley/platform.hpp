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
// 這次中斷是否為控制器的假中斷（spurious）。8259A 在中斷訊號於確認週期前
// 消失時，仍會送出 IRQ7 或 IRQ15；這種中斷不可以執行處理常式，也不可以送出
// end-of-interrupt。平台若無此問題一律回傳 false。
//
// Whether this interrupt is one the controller raised spuriously. An 8259A
// still reports IRQ7 or IRQ15 when the interrupt signal disappears before the
// acknowledge cycle; such an interrupt must run no handler and receive no
// end-of-interrupt. A platform without this behaviour always returns false.
bool spurious_interrupt(Irq);

// 平台計時器自開機以來累積的中斷次數與中斷頻率。沒有計時器驅動程式的平台
// 回報 0，因此通用程式碼可以直接以 timer_frequency() 是否為 0 判斷。
//
// The platform timer's interrupt count since boot and its interrupt rate. A
// platform with no timer driver reports zero for both, so generic code can
// simply test timer_frequency() against zero.
std::uint64_t timer_ticks();
unsigned timer_frequency();

// 平台電源控制；韌體不支援時退回停機。
// Platform power control; falls back to halting when firmware has no support.
[[noreturn]] void power_off();
[[noreturn]] void restart();

} // namespace shirley::platform
