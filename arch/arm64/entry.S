.section .text.boot
.global _start
.type _start, %function
_start:
    // 設定核心堆疊，並以空的啟動資訊進入 C++ 核心入口。
    ldr x0, =stack_top
    mov sp, x0
    mov x0, #0
    bl kernel_main
1:  wfe
    b 1b
.section .bss
// 保留 16 KiB 的啟動堆疊，並依頁面大小對齊。
.align 12
stack:
    .skip 16384
stack_top:
