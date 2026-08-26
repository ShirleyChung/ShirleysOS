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
// The first vector after remapping; vectors 0-31 belong to CPU exceptions.
constexpr unsigned base_vector = 32;

// 重新對應向量並先遮罩所有 IRQ。
// Remap the vectors and start with every IRQ masked.
void pic_initialize();
void pic_mask(unsigned irq);
void pic_unmask(unsigned irq);
// 通知控制器中斷已處理完畢。
// Tell the controller the interrupt has been handled.
void pic_end_of_interrupt(unsigned irq);

} // namespace shirley::platform::pc
