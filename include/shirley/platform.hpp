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
// 中斷控制器把所有裝置中斷集中到單一架構向量時，irq_vector() 回傳這個值。
// GIC 與 Apple AIC 屬於這一類：由控制器驅動程式自己掛上那個向量、辨識來源，
// 再呼叫 irq::dispatch()。此時 IRQ 層絕對不能自行註冊向量處理常式，否則每個
// IRQ 都會掛到同一個向量互相覆蓋，還會蓋掉控制器的分辨常式。
//
// What irq_vector() returns when the interrupt controller funnels every device
// interrupt into a single architecture vector. A GIC and Apple's AIC are of
// this kind: the controller driver hooks that vector itself, identifies the
// source, and calls irq::dispatch(). The IRQ layer must not register a vector
// handler of its own in that case, or every IRQ would land on the same vector,
// overwriting both each other and the controller's demultiplexing handler.
constexpr unsigned demultiplexed_vector = ~0u;

// 將平台 IRQ 對應到架構中斷向量；控制器自行分辨來源時回傳
// demultiplexed_vector。
//
// Map a platform IRQ onto an architecture interrupt vector, or
// demultiplexed_vector when the controller identifies the source itself.
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
