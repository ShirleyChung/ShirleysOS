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

# long x86_64_save_and_enter(uint64_t entry, uint64_t stack,
#                            uint16_t code_selector, uint16_t data_selector,
#                            UserContext* context)
# rdi=entry, rsi=stack, dx=code_selector, cx=data_selector, r8=context
.global x86_64_save_and_enter
x86_64_save_and_enter:
    # 先保存核心的被呼叫者保存暫存器、返回位址、返回後的堆疊指標與旗標，
    # exit 系統呼叫才有辦法跳回這裡並讓本函式正常返回。r8 是 context 指標，
    # rax 當暫存器；rcx/rdx 保留給下面的段選擇子，不能動。
    #
    # Save the kernel's callee-saved registers, the return address, the stack
    # pointer as it will be after returning, and the flags, so the exit syscall
    # can jump back here and let this function return normally. r8 is the
    # context pointer and rax is scratch; rcx/rdx are left alone for the segment
    # selectors below.
    mov [r8 + 0], rbx
    mov [r8 + 8], rbp
    mov [r8 + 16], r12
    mov [r8 + 24], r13
    mov [r8 + 32], r14
    mov [r8 + 40], r15
    mov rax, [rsp]      # 返回位址 / return address
    mov [r8 + 56], rax
    lea rax, [rsp + 8]  # 返回後呼叫端的 rsp / caller rsp after this returns
    mov [r8 + 48], rax
    pushfq
    pop rax
    mov [r8 + 64], rax  # rflags
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

# [[noreturn]] void x86_64_restore_and_exit(UserContext* context, long status)
# rdi=context, rsi=status
.global x86_64_restore_and_exit
x86_64_restore_and_exit:
    # 從 context 還原被呼叫者保存暫存器，把 status 放進回傳暫存器，切回核心
    # 堆疊並還原旗標（含 IF），最後跳回 x86_64_save_and_enter 的返回位址——
    # 對呼叫端而言，那次呼叫就這樣以 status 作為回傳值返回。
    #
    # Restore the callee-saved registers from context, put status in the return
    # register, switch back to the kernel stack and restore the flags (IF
    # included), then jump to x86_64_save_and_enter's return address: to the
    # caller, that call simply returns with status as its value.
    mov rbx, [rdi + 0]
    mov rbp, [rdi + 8]
    mov r12, [rdi + 16]
    mov r13, [rdi + 24]
    mov r14, [rdi + 32]
    mov r15, [rdi + 40]
    mov rcx, [rdi + 56] # 返回位址 / return address
    mov rdx, [rdi + 64] # rflags
    mov rax, rsi        # 回傳值 = status / return value = status
    mov rsp, [rdi + 48] # 切回核心堆疊 / switch back to the kernel stack
    push rdx
    popfq               # 還原旗標；IF 若被設起，中斷會在下一條指令之後才認得
    jmp rcx
