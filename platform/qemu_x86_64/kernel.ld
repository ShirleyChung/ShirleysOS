ENTRY(_start)
SECTIONS {
  /* 開機磁區把核心讀到 0x10000，並直接跳到映像的第一個位元組。 */
  /* The boot sector loads the kernel at 0x10000 and jumps to its first byte. */
  . = 0x10000;
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
