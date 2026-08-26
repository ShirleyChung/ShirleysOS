SECTIONS {
  /* BIOS 把開機磁區載入 0x7c00 並從該處執行。 */
  /* BIOS loads the boot sector at 0x7c00 and starts executing there. */
  . = 0x7c00;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  /DISCARD/ : { *(.comment) *(.note*) }
}
