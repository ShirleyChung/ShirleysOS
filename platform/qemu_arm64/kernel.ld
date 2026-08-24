ENTRY(_start)
SECTIONS { . = 0x40080000; .text : { *(.text.boot) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(COMMON) *(.bss*) } }
