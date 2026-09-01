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
    // Exceptions from EL0 use SP_EL0; current-EL exceptions use the EL1 stack
    // on which this frame was allocated.
    cmp x0, #8
    b.lo 1f
    mrs x1, sp_el0
    b 2f
1:  add x1, sp, #frame_size
2:
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
    ldr x1, [sp, #(36 * 8)]
    cmp x1, #8
    b.lo 3f
    ldr x1, [sp, #(31 * 8)]
    msr sp_el0, x1
3:
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

// long arm64_save_and_enter(uint64_t entry, uint64_t stack, UserContext* context)
// x0=entry, x1=stack, x2=context
.global arm64_save_and_enter
arm64_save_and_enter:
    // 先保存核心的被呼叫者保存暫存器、返回位址、堆疊指標與中斷遮罩狀態，
    // exit 系統呼叫才有辦法跳回這裡並讓本函式正常返回。
    // Save the kernel's callee-saved registers, the return address, the stack
    // pointer, and the interrupt mask state, so the exit syscall can jump back
    // here and let this function return normally.
    stp x19, x20, [x2, #0]
    stp x21, x22, [x2, #16]
    stp x23, x24, [x2, #32]
    stp x25, x26, [x2, #48]
    stp x27, x28, [x2, #64]
    stp x29, x30, [x2, #80]
    mov x3, sp
    str x3, [x2, #96]
    mrs x3, daif
    str x3, [x2, #104]
    // SPSR 的 M[3:0] 為 0 代表返回 EL0t，並且不遮罩 IRQ/FIQ。
    // M[3:0] of SPSR set to 0 means return to EL0t with IRQ and FIQ unmasked.
    msr elr_el1, x0
    msr sp_el0, x1
    mov x3, #0
    msr spsr_el1, x3
    isb
    eret

// [[noreturn]] void arm64_restore_and_exit(UserContext* context, long status)
// x0=context, x1=status
.global arm64_restore_and_exit
arm64_restore_and_exit:
    // 從 context 還原被呼叫者保存暫存器與堆疊，還原核心的中斷遮罩狀態，把
    // status 放進回傳暫存器，最後返回 arm64_save_and_enter 的呼叫端——對它而
    // 言，那次呼叫就這樣以 status 作為回傳值返回。
    //
    // Restore the callee-saved registers and stack from context, restore the
    // kernel's interrupt mask state, put status in the return register, then
    // return to arm64_save_and_enter's caller: to it, that call simply returns
    // with status as its value.
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldp x29, x30, [x0, #80]
    ldr x3, [x0, #96]
    mov sp, x3
    ldr x3, [x0, #104]
    msr daif, x3
    mov x0, x1
    ret
