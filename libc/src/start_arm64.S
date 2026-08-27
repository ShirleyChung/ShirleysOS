.text
.global _start
_start:
    bl main
    mov x1, x0
    mov x8, #2
    svc #0
1:  wfi
    b 1b
