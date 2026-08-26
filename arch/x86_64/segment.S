.intel_syntax noprefix
.section .text

# void x86_64_load_gdt(const void* pointer, uint16_t code_selector, uint16_t data_selector)
.global x86_64_load_gdt
x86_64_load_gdt:
    lgdt [rdi]
    # 先換掉資料段，再以遠返回重新載入 CS。
    # Swap the data segments first, then reload CS with a far return.
    mov ds, dx
    mov es, dx
    mov ss, dx
    mov fs, dx
    mov gs, dx
    movzx rax, si
    push rax
    lea rax, [rip + 1f]
    push rax
    retfq
1:  ret

# void x86_64_load_idt(const void* pointer)
.global x86_64_load_idt
x86_64_load_idt:
    lidt [rdi]
    ret

# void x86_64_load_tss(uint16_t selector)
.global x86_64_load_tss
x86_64_load_tss:
    ltr di
    ret

# [[noreturn]] void x86_64_enter_userspace(uint64_t entry, uint64_t stack,
#                                          uint16_t code_selector, uint16_t data_selector)
.global x86_64_enter_userspace
x86_64_enter_userspace:
    # 使用者資料段先就位，再堆出 IRET 需要的五個欄位。
    # Put the user data segments in place, then build the five fields IRET
    # expects on the stack.
    mov ds, cx
    mov es, cx
    mov fs, cx
    mov gs, cx
    movzx rax, cx
    push rax            # SS：使用者堆疊段 / user stack segment
    push rsi            # RSP：使用者堆疊指標 / user stack pointer
    push 0x202          # RFLAGS：保留位元加上 IF / reserved bit plus IF
    movzx rax, dx
    push rax            # CS：使用者程式碼段 / user code segment
    push rdi            # RIP：使用者程式進入點 / user entry point
    iretq
