.section .text.boot
.global _start
.type _start, %function
_start:
    // UEFI 韌體會把中斷開著，而此時例外向量表還是韌體的，因此一律先遮罩。
    // UEFI firmware leaves interrupts enabled while the vector table still
    // belongs to the firmware, so mask them unconditionally.
    msr daifset, #0xf
    // QEMU virt 與 m1n1 都以 x0 帶入韌體資料指標（裝置樹或 boot_args）。
    // Both QEMU virt and m1n1 pass a firmware data pointer in x0: a device
    // tree or an Apple boot_args structure.
    mov x19, x0
    // 設定核心堆疊。
    // Establish the kernel stack.
    adrp x0, stack_top
    add x0, x0, :lo12:stack_top
    mov sp, x0
    // 純二進位與 ELF 映像都不含 .bss 內容，必須自行清零。
    // Neither a flat binary nor an ELF image carries .bss contents, so zero it
    // here.
    adrp x0, __bss_start
    add x0, x0, :lo12:__bss_start
    adrp x1, __bss_end
    add x1, x1, :lo12:__bss_end
1:  cmp x0, x1
    b.hs 2f
    str xzr, [x0], #8
    b 1b
    // 由平台層把韌體資料轉換成通用 BootInfo，再進入核心。
    // Let the platform layer turn the firmware data into a neutral BootInfo,
    // then enter the kernel.
2:  mov x0, x19
    bl shirley_platform_boot_info
    bl kernel_main
3:  wfe
    b 3b

.section .bss
// 保留 32 KiB 的啟動堆疊，並依頁面大小對齊。
// Reserve a 32 KiB boot stack, page aligned.
.align 12
stack:
    .skip 32768
stack_top:
