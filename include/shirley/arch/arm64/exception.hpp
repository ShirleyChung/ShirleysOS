#pragma once

#include <cstdint>

// AArch64 沒有 x86 那種 256 項中斷向量表，而是 16 個例外入口。
// arch::register_interrupt_handler() 在 ARM64 上使用這裡的入口編號；
// 中斷控制器驅動程式註冊在 IRQ 入口，再自行分辨實際的裝置中斷。
//
// AArch64 has no 256-entry interrupt table like x86; it has 16 exception
// vector entries. On ARM64 arch::register_interrupt_handler() takes one of the
// entry numbers below. Interrupt controller drivers register on an IRQ entry
// and work out for themselves which device raised the interrupt.
namespace shirley::arch::arm64 {

enum Vector : unsigned {
    current_el_sp0_sync = 0,
    current_el_sp0_irq,
    current_el_sp0_fiq,
    current_el_sp0_serror,
    current_el_spx_sync,
    current_el_spx_irq,
    current_el_spx_fiq,
    current_el_spx_serror,
    lower_el_aarch64_sync,
    lower_el_aarch64_irq,
    lower_el_aarch64_fiq,
    lower_el_aarch64_serror,
    lower_el_aarch32_sync,
    lower_el_aarch32_irq,
    lower_el_aarch32_fiq,
    lower_el_aarch32_serror,
    vector_table_size,
};

} // namespace shirley::arch::arm64
