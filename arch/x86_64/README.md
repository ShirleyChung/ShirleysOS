# x86_64 architecture layer

ISA-specific code lives here. It must not leak CR3, PML4, GDT selectors, or
register conventions into `kernel/`.

| File | Responsibility |
| ---- | -------------- |
| `boot.S` | 512-byte BIOS boot sector: E820 memory map, disk read, 1 GiB identity map, long-mode entry |
| `entry.S` | Kernel entry at the start of the image: boot stack, `.bss` zeroing, boot protocol call |
| `arch.cpp` | The `shirley::arch` interface for this ISA |
| `cpu.cpp` | CPUID vendor string, NX and SSE enablement, CR0 write protect |
| `gdt.cpp` | GDT and TSS, including the ring 3 selectors |
| `idt.cpp` | 256-entry IDT, handler registry, exception reporting |
| `interrupt.S` | The 256 interrupt stubs and the common register-saving entry |
| `segment.S` | `lgdt`/`lidt`/`ltr` wrappers and the `iretq` transition to ring 3 |
| `paging.cpp` | Four-level page tables implementing `memory::AddressSpace` |

`interrupt.S` emits one fixed-size stub per vector so the IDT can compute an
entry address arithmetically. The stride is not written down twice: the
assembler exports `x86_64_isr_stubs` and `x86_64_isr_stubs_end`, and `idt.cpp`
divides by the vector count.

`paging.cpp` walks page tables through physical addresses, which is only valid
because the boot sector identity-maps the first 1 GiB. Moving the kernel to a
higher-half mapping means revisiting `table_of()`.

Public headers other x86_64 code may include are in
`include/shirley/arch/x86_64/`: `port_io.hpp` for I/O ports (an ISA feature
that PC device drivers legitimately need) and `paging.hpp`.
