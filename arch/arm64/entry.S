.section .text.boot
.global _start
.type _start, %function
_start:
    ldr x0, =stack_top
    mov sp, x0
    mov x0, #0
    bl kernel_main
1:  wfe
    b 1b
.section .bss
.align 12
stack:
    .skip 16384
stack_top:
