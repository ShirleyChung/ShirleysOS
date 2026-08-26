ENTRY(_start)
SECTIONS {
  /* m1n1 以 0x10000 對齊把映像載入 RAM；實際位址由載入器決定，
     這裡的基底位址只影響靜態符號，M8 導入位置無關載入後會改為 PIE。 */
  /* m1n1 loads the image into RAM at a 0x10000 alignment. The real address is
     chosen by the loader, so this base only affects static symbols; M8 moves
     to a position-independent image. */
  . = 0x800000000;
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
