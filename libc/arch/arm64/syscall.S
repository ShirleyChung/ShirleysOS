/* long long shirley_syscall(long long number, ...); */
.text
.global shirley_syscall
shirley_syscall:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    svc #0
    ret
