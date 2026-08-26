ENTRY(_start)
SECTIONS {
  /* UEFI 韌體自己會使用低位記憶體，因此核心放在 2 MiB 這個一般都空著、
     而且對齊大分頁的位址；載入器會以 AllocateAddress 向韌體要下這段範圍。 */
  /* UEFI firmware uses low memory itself, so the kernel sits at 2 MiB: an
     address that is normally free and is aligned to a huge page. The loader
     claims the range from the firmware with AllocateAddress. */
  . = 0x200000;
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
