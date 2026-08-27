#pragma once

#include <cstdint>

// 裝置驅動程式與中斷控制器之間的通用介面。驅動程式只認得平台 IRQ 編號，
// 不知道背後是 8259A、APIC/IOAPIC、MSI/MSI-X 還是 ARM64 的 GIC；向量對應、
// 假中斷判斷與 end-of-interrupt 全部由平台層負責。
//
// The generic interface between device drivers and the interrupt controller.
// A driver knows only a platform IRQ number, never whether an 8259A, an
// APIC/IOAPIC, MSI/MSI-X, or an ARM64 GIC sits behind it. Vector mapping,
// spurious-interrupt detection, and end-of-interrupt all belong to the
// platform layer.
namespace shirley::irq {

// 中斷處理常式；在中斷情境中執行，因此必須簡短且不得阻塞。
// An interrupt handler. It runs in interrupt context, so it must be short and
// must never block.
using Handler = void (*)(unsigned irq, void* context);

// 可註冊的 IRQ 編號上限。8259A 只用到 0-15，GICv2 的 SPI 在 QEMU virt 上會
// 用到 96 附近，之後的 IOAPIC 全域中斷編號也直接沿用同一張表。超出這個範圍
// 的中斷仍然會被確認並送出 EOI，只是沒有處理常式可以註冊。
//
// The upper bound on registerable IRQ numbers. An 8259A uses only 0-15, GICv2
// SPIs reach the high nineties on QEMU virt, and a later IOAPIC reuses this
// same table for its global system interrupt numbers. An interrupt past this
// range is still acknowledged and given an EOI; it simply has no handler slot.
constexpr unsigned max_irq_count = 256;

// 清空處理常式表；必須在任何驅動程式註冊之前呼叫。
// Clear the handler table. Must run before any driver registers.
void initialize();

// 註冊處理常式並解除該 IRQ 的遮罩。IRQ 超出範圍、handler 為空，或該 IRQ
// 已被佔用時回傳 false。
//
// Register a handler and unmask the IRQ. Returns false when the IRQ is out of
// range, the handler is null, or the IRQ is already taken.
bool request(unsigned irq, Handler handler, void* context = nullptr);
// 遮罩該 IRQ 並移除處理常式。
// Mask the IRQ and remove its handler.
void release(unsigned irq);
bool registered(unsigned irq);

// 由中斷控制器路徑呼叫，執行處理常式後送出 end-of-interrupt。
// 一個向量對應一個 IRQ 的控制器（例如 8259A）由本層自動接上；之後像 GIC 或
// AIC 那種把多個裝置中斷集中到單一向量的控制器，則由其驅動程式辨識來源後
// 自行呼叫這個函式。
//
// Called from the interrupt controller path: it runs the handler and then
// signals end-of-interrupt. A controller with one vector per IRQ, such as the
// 8259A, is wired up by this layer automatically. A controller that funnels
// many device interrupts into a single vector, such as a GIC or Apple's AIC,
// identifies the source itself and then calls this function.
void dispatch(unsigned irq);

// 已分派的中斷次數，供診斷使用。
// How many interrupts have been dispatched, for diagnostics.
std::uint64_t count(unsigned irq);

} // namespace shirley::irq
