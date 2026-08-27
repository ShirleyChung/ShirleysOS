# QEMU ARM64 platform

The Tier 1 ARM64 reference machine: QEMU `virt`.

| File | Responsibility |
| ---- | -------------- |
| `boot_protocol.cpp` | Reads the device tree passed in `x0` into `BootInfo` |
| `console.cpp` | PL011 UART |
| `platform.cpp` | Machine identity, capabilities, IRQ routing, PSCI power off and restart |
| `kernel.ld` | Kernel at 0x40080000 with `.text.boot` first and `.bss` markers |

The device tree parser is in `platform/firmware/`. If firmware supplies no
usable device tree the boot protocol falls back to a conservative fixed RAM
window rather than guessing a larger one.

The interrupt controller is a GICv2 and the timer is the ARM architected
timer; both drivers live in `platform/arm/` because ARM defines them rather
than this machine. This directory only supplies the two addresses QEMU virt
puts its GICv2 at and decides the bring-up order — the controller first, then
the timer, because unmasking PPI 30 is the distributor's job.

Those two addresses are compile-time constants. A real machine should read
them from the device tree's interrupt controller node, which needs generic
node lookup that `platform/firmware/fdt.cpp` does not have yet.
