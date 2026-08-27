/* long long shirley_syscall(long long number, ...); */
.intel_syntax noprefix
.text
.global shirley_syscall
shirley_syscall:
    /* The kernel entry currently uses the conventional int 0x80 ABI. */
    mov eax, edi
    mov rdi, rsi
    mov rsi, rdx
    mov rdx, rcx
    int 0x80
    ret
