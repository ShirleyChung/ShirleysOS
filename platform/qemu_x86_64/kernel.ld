ENTRY(_start)
SECTIONS {
  /* 開機磁區把核心讀到 0x10000，並直接跳到映像的第一個位元組。 */
  /* The boot sector loads the kernel at 0x10000 and jumps to its first byte. */
  . = 0x10000;
  __kernel_start = .;
  .text : { *(.text.boot) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  /* .bss 從 8 MiB 開始，而不是緊接在映像之後。理由有二。低位記憶體只到 0x9FC00，
     之上是 EBDA、VGA 與 BIOS ROM；核心堆積在 .bss 裡，接在映像後面會直接蓋過
     那一段。再者，user 程式的虛擬位址落在 0x1000 到 0x800000（見
     kernel/elf/user_loader.cpp），而行程位址空間會把頁面配置器管理的實體記憶體
     identity map 進去；boot_protocol 把 0 到 __kernel_end 整段保留起來，所以
     .bss 放在 8 MiB 之上，配置器就不可能交出一個實體位址與 user 映像重疊、
     identity mapping 會被 user 映射蓋掉的頁面。開機磁區的 identity mapping
     涵蓋前 1 GiB。 */
  /* .bss starts at 8 MiB rather than right after the image, for two reasons.
     Low memory ends at 0x9FC00, with the EBDA, VGA memory and the BIOS ROM
     above it, and the kernel heap lives in .bss: placed right after the image
     it would run straight over them. And user programs live at virtual
     addresses 0x1000 to 0x800000 (see kernel/elf/user_loader.cpp) while a
     process address space identity-maps the RAM the page allocator manages.
     boot_protocol reserves everything from 0 to __kernel_end, so with .bss
     above 8 MiB the allocator can never hand out a page whose physical address
     overlaps the user image and whose identity mapping a user mapping would
     hide. The boot sector identity-maps the first 1 GiB. */
  .bss 0x800000 (NOLOAD) : ALIGN(4096) {
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
