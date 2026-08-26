// AArch64 EL1 例外向量表。表格本身要 2 KiB 對齊，
// 16 個入口各佔 128 位元組，順序由 VBAR_EL1 的架構定義決定。
//
// The AArch64 EL1 exception vector table. The table is 2 KiB aligned and its
// 16 entries are 128 bytes each; the order is fixed by the architecture's
// definition of VBAR_EL1.

// ExceptionFrame 的大小；欄位偏移量與 internal.hpp 一致。
// The size of ExceptionFrame; field offsets match internal.hpp.
.set frame_size, 304

.macro vector_entry index
    .align 7
    sub sp, sp, #frame_size
    stp x0, x1, [sp, #(0 * 8)]
    mov x0, #\index
    b arm64_exception_common
.endm

.section .text
.align 11
.global arm64_exception_vectors
arm64_exception_vectors:
    // 目前例外層級，使用 SP_EL0。
    // Current exception level, using SP_EL0.
    vector_entry 0
    vector_entry 1
    vector_entry 2
    vector_entry 3
    // 目前例外層級，使用 SP_ELx。
    // Current exception level, using SP_ELx.
    vector_entry 4
    vector_entry 5
    vector_entry 6
    vector_entry 7
    // 來自較低例外層級的 AArch64 執行狀態。
    // From a lower exception level in AArch64 execution state.
    vector_entry 8
    vector_entry 9
    vector_entry 10
    vector_entry 11
    // 來自較低例外層級的 AArch32 執行狀態。
    // From a lower exception level in AArch32 execution state.
    vector_entry 12
    vector_entry 13
    vector_entry 14
    vector_entry 15

// x0 為向量編號，x0/x1 已存入框架。
// x0 holds the vector number; x0 and x1 are already saved in the frame.
arm64_exception_common:
    stp x2, x3, [sp, #(2 * 8)]
    stp x4, x5, [sp, #(4 * 8)]
    stp x6, x7, [sp, #(6 * 8)]
    stp x8, x9, [sp, #(8 * 8)]
    stp x10, x11, [sp, #(10 * 8)]
    stp x12, x13, [sp, #(12 * 8)]
    stp x14, x15, [sp, #(14 * 8)]
    stp x16, x17, [sp, #(16 * 8)]
    stp x18, x19, [sp, #(18 * 8)]
    stp x20, x21, [sp, #(20 * 8)]
    stp x22, x23, [sp, #(22 * 8)]
    stp x24, x25, [sp, #(24 * 8)]
    stp x26, x27, [sp, #(26 * 8)]
    stp x28, x29, [sp, #(28 * 8)]
    str x30, [sp, #(30 * 8)]
    // 記錄例外發生前的堆疊指標與處理器狀態。
    // Record the stack pointer and processor state from before the exception.
    add x1, sp, #frame_size
    str x1, [sp, #(31 * 8)]
    mrs x1, elr_el1
    str x1, [sp, #(32 * 8)]
    mrs x1, spsr_el1
    str x1, [sp, #(33 * 8)]
    mrs x1, esr_el1
    str x1, [sp, #(34 * 8)]
    mrs x1, far_el1
    str x1, [sp, #(35 * 8)]
    str x0, [sp, #(36 * 8)]

    mov x0, sp
    bl arm64_exception_dispatch

    // 處理常式可以修改返回位址與狀態，因此回寫後再還原暫存器。
    // A handler may change the return address or state, so write those back
    // before restoring the registers.
    ldr x1, [sp, #(32 * 8)]
    msr elr_el1, x1
    ldr x1, [sp, #(33 * 8)]
    msr spsr_el1, x1
    ldp x0, x1, [sp, #(0 * 8)]
    ldp x2, x3, [sp, #(2 * 8)]
    ldp x4, x5, [sp, #(4 * 8)]
    ldp x6, x7, [sp, #(6 * 8)]
    ldp x8, x9, [sp, #(8 * 8)]
    ldp x10, x11, [sp, #(10 * 8)]
    ldp x12, x13, [sp, #(12 * 8)]
    ldp x14, x15, [sp, #(14 * 8)]
    ldp x16, x17, [sp, #(16 * 8)]
    ldp x18, x19, [sp, #(18 * 8)]
    ldp x20, x21, [sp, #(20 * 8)]
    ldp x22, x23, [sp, #(22 * 8)]
    ldp x24, x25, [sp, #(24 * 8)]
    ldp x26, x27, [sp, #(26 * 8)]
    ldp x28, x29, [sp, #(28 * 8)]
    ldr x30, [sp, #(30 * 8)]
    add sp, sp, #frame_size
    eret

// [[noreturn]] void arm64_enter_userspace(uint64_t entry, uint64_t stack)
.global arm64_enter_userspace
arm64_enter_userspace:
    // SPSR 的 M[3:0] 為 0 代表返回 EL0t，並且不遮罩 IRQ/FIQ。
    // M[3:0] of SPSR set to 0 means return to EL0t with IRQ and FIQ unmasked.
    msr elr_el1, x0
    msr sp_el0, x1
    mov x2, #0
    msr spsr_el1, x2
    isb
    eret
