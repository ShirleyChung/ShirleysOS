.intel_syntax noprefix
.section .text

# 256 個中斷入口，每個固定 16 位元組，因此 IDT 可以用
# x86_64_isr_stubs + vector * 16 直接算出位址。
# 會由 CPU 推入錯誤碼的向量不再自行補 0，讓堆疊配置保持一致。
#
# 256 interrupt entry points, each exactly 16 bytes, so the IDT can compute an
# address as x86_64_isr_stubs + vector * 16. A vector where the CPU already
# pushes an error code does not push a dummy one, so the stack layout stays
# identical either way.
.align 16
.global x86_64_isr_stubs
x86_64_isr_stubs:
.set vector, 0
.rept 256
    .align 16
    .if (vector == 8) || (vector >= 10 && vector <= 14) || (vector == 17) || (vector == 21) || (vector == 29) || (vector == 30)
        nop
    .else
        push 0
    .endif
    push OFFSET vector
    jmp x86_64_interrupt_entry
    .set vector, vector + 1
.endr
.align 16
# idt.cpp 以 (end - start) / 256 算出實際間距，不必把 16 寫死在兩個地方。
# idt.cpp derives the stride as (end - start) / 256, so 16 is not written down
# in two places.
.global x86_64_isr_stubs_end
x86_64_isr_stubs_end:

# 依 InterruptFrame 的欄位順序保存暫存器後轉交 C++ 分派函式。
# Save the registers in InterruptFrame's field order, then hand off to the C++
# dispatcher.
x86_64_interrupt_entry:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp
    call x86_64_interrupt_dispatch
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    # 丟棄向量編號與錯誤碼。
    # Drop the vector number and the error code.
    add rsp, 16
    iretq
