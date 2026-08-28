# ARM64 architecture layer

ISA-specific code for AArch64, shared by every ARM64 platform including
QEMU `virt` and Apple Silicon. Machine differences — UART, interrupt
controller, power control — belong in `platform/`, not here.

| File | Responsibility |
| ---- | -------------- |
| `entry.S` | Kernel entry: boot stack, `.bss` zeroing, boot protocol call |
| `arch.cpp` | The `shirley::arch` interface for this ISA |
| `cpu.cpp` | MIDR_EL1 implementer decoding, FP/SIMD access for EL0 |
| `exception.S` | The 2 KiB-aligned EL1 vector table and register save/restore |
| `exception.cpp` | Vector installation, handler registry, fault reporting |
| `paging.cpp` | Stage 1 translation tables implementing `memory::AddressSpace`, plus `mmu_enable()` |

AArch64 has 16 exception vector entries rather than a 256-entry table, so
`arch::register_interrupt_handler()` takes an entry number from
`include/shirley/arch/arm64/exception.hpp`. Every device interrupt arrives on
an IRQ entry; the interrupt-controller driver in `platform/` is responsible
for working out which device raised it.

The hardware reports an interrupt through a different entry depending on the
exception level it interrupted, but an interrupt does not become a different
interrupt because a user process was running: same line, same device, same
handler. `exception.cpp` therefore maps a lower-EL IRQ or FIQ onto the
current-EL entry, so a driver registers once and keeps working while userspace
runs. Synchronous exceptions and SErrors are deliberately left alone — a fault
in a user process and a fault in the kernel are not the same event.

The MMU is not enabled at boot. `mmu_enable()` programs MAIR, TCR, and TTBR0
and turns on SCTLR_EL1.M, but nothing calls it yet: switching the running
kernel onto its own translation tables is milestone M1. Until then
`PageTable` builds tables that are correct but inactive, and `paging.cpp`
reaches them through physical addresses.
