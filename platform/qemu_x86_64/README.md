# QEMU x86_64 platform

The Tier 1 x86_64 reference machine: QEMU PC with SeaBIOS firmware.

| File | Responsibility |
| ---- | -------------- |
| `boot_protocol.cpp` | Converts the boot sector's E820 table into `BootInfo` |
| `console.cpp` | COM1 serial console |
| `platform.cpp` | Machine identity, capabilities, IRQ routing, ACPI power off |
| `kernel.ld` | Kernel at 0x10000 with `.text.boot` first and `.bss` markers |
| `boot.ld` | Boot sector at 0x7c00 |

Device hardware shared with real PCs lives in `platform/pc/`, and the E820
parser lives in `platform/firmware/`. The boot sector reads the kernel with a
single BIOS call, so the build fails if the kernel image grows past 127
sectors instead of shipping a silently truncated image.

UEFI/OVMF boot replaces the BIOS boot sector in a later milestone.
