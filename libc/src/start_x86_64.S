.intel_syntax noprefix
.text
.global _start
_start:
    xor ebp, ebp
    call main
    mov edi, eax
    mov eax, 2
    int 0x80
1:  pause
    jmp 1b
