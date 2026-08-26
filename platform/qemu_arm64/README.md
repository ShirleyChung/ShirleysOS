# QEMU ARM64 platform

The Tier 1 ARM64 reference machine: QEMU `virt`.

| File | Responsibility |
| ---- | -------------- |
| `boot_protocol.cpp` | Reads the device tree passed in `x0` into `BootInfo` |
| `console.cpp` | PL011 UART |
| `platform.cpp` | Machine identity, capabilities, PSCI power off and restart |
| `kernel.ld` | Kernel at 0x40080000 with `.text.boot` first and `.bss` markers |

The device tree parser is in `platform/firmware/`. If firmware supplies no
usable device tree the boot protocol falls back to a conservative fixed RAM
window rather than guessing a larger one.

GICv2 integration is milestone M2, so `platform::capabilities()` currently
reports no interrupt controller and every device IRQ stays masked.
