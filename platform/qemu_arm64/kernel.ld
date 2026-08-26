ENTRY(_start)
SECTIONS {
  /* QEMU virt 的 RAM 從 0x40000000 開始，-kernel 會把映像放在 0x40080000。 */
  /* RAM on QEMU virt starts at 0x40000000 and -kernel loads at 0x40080000. */
  . = 0x40080000;
  __kernel_start = .;
  .text : { *(.text.boot) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss ALIGN(4096) : {
    __bss_start = .;
    *(COMMON)
    *(.bss*)
    . = ALIGN(16);
    __bss_end = .;
  }
  /* 核心映像（含 .bss）的結尾，供開機資訊標記保留記憶體。 */
  /* End of the kernel image including .bss, used to reserve memory. */
  __kernel_end = .;
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
