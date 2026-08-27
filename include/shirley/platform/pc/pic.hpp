#pragma once

#include <cstdint>

// 傳統 PC 的 8259A 可程式化中斷控制器，QEMU PC 與實體 PC 平台共用。
// The legacy PC 8259A programmable interrupt controller, shared by the QEMU PC
// and physical PC platforms.
namespace shirley::platform::pc {

// 兩顆 8259 串接後提供 IRQ 0-15。
// Two cascaded 8259s provide IRQ 0-15.
constexpr unsigned irq_count = 16;
// 重新對應後的第一個中斷向量；前 32 個向量保留給 CPU 例外。
// 主控制器落在 0x20-0x27，從控制器落在 0x28-0x2f。
//
// The first vector after remapping; vectors 0-31 belong to CPU exceptions. The
// master lands on 0x20-0x27 and the slave on 0x28-0x2f.
constexpr unsigned base_vector = 32;
// 從控制器串接在主控制器的 IRQ2 上，因此 IRQ2 不是裝置中斷。
// The slave cascades into the master's IRQ2, so IRQ2 is not a device
// interrupt.
constexpr unsigned cascade_irq = 2;
// 兩顆控制器最高優先權的那條線在訊號提早消失時會產生假中斷。
// The lowest-priority line on each controller is the one that reports a
// spurious interrupt when the signal disappears early.
constexpr unsigned master_spurious_irq = 7;
constexpr unsigned slave_spurious_irq = 15;

// 重新對應向量、遮罩所有裝置 IRQ，只保留串接線。
// Remap the vectors and mask every device IRQ, leaving only the cascade line
// open.
void pic_initialize();
void pic_mask(unsigned irq);
void pic_unmask(unsigned irq);
// 通知控制器中斷已處理完畢。
// Tell the controller the interrupt has been handled.
void pic_end_of_interrupt(unsigned irq);
// 判斷 IRQ7 或 IRQ15 是否為假中斷。從控制器的假中斷仍須向主控制器送出
// end-of-interrupt，那件事由這個函式自己完成，呼叫端不必再處理。
//
// Decide whether an IRQ7 or IRQ15 is spurious. A spurious slave interrupt
// still owes the master an end-of-interrupt; this function issues it, so the
// caller does not have to.
bool pic_spurious(unsigned irq);

} // namespace shirley::platform::pc
