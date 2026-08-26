ENTRY(_start)
SECTIONS {
  /* QEMU virt 的 RAM 從 0x40000000 開始。EDK2 韌體自己會使用 RAM 的最低段，
     因此核心放在 RAM 起點往上 16 MiB 處，載入器再以 AllocateAddress 取得它。 */
  /* RAM on QEMU virt starts at 0x40000000. EDK2 firmware uses the lowest part
     of RAM itself, so the kernel sits 16 MiB above the start of RAM and the
     loader claims that range with AllocateAddress. */
  . = 0x41000000;
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
