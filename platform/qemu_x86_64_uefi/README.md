# QEMU x86_64 UEFI platform

The same machine as `platform/qemu_x86_64/` — a QEMU PC — but booted by OVMF
UEFI firmware instead of a BIOS boot sector.

| File | Responsibility |
| ---- | -------------- |
| `boot_protocol.cpp` | Validates the `BootHandoff` from the ShirleyOS UEFI loader |
| `platform.cpp` | Machine identity, capabilities, IRQ routing, ACPI power off |
| `kernel.ld` | Kernel at 2 MiB, an address the firmware will normally hand over |

Because the machine is unchanged, the device drivers are shared verbatim with
the BIOS platform: `platform/pc/serial_console.cpp` for COM1 and
`platform/pc/pic.cpp` for the 8259. Only the firmware handoff differs.

The boot protocol here parses nothing. All of the firmware work — reading the
kernel, the memory map, the framebuffer — happens in `boot/uefi/` before
`ExitBootServices`, so the kernel receives a `BootHandoff` that is already in
the neutral format and only has to confirm it came from that loader.

The kernel is linked at 2 MiB rather than the BIOS platform's 0x10000 because
UEFI firmware uses low memory itself. The loader claims that range with
`AllocatePages(AllocateAddress)`, so a firmware that has already taken it
fails loudly instead of corrupting itself.

Once the kernel is running, the UEFI runtime services are not used. The kernel
has already called `ExitBootServices`, so power off drives the ACPI register
directly rather than calling `ResetSystem`.
