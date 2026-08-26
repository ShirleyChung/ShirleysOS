# QEMU ARM64 UEFI platform

The same machine as `platform/qemu_arm64/` — QEMU `virt` — but booted by EDK2
UEFI firmware (AAVMF) instead of QEMU's `-kernel` loader.

| File | Responsibility |
| ---- | -------------- |
| `boot_protocol.cpp` | Validates the `BootHandoff` from the ShirleyOS UEFI loader |
| `platform.cpp` | Machine identity, capabilities, PSCI power off and restart |
| `kernel.ld` | Kernel 16 MiB above the start of RAM, clear of the firmware |

The machine is unchanged, so the PL011 driver in `platform/qemu_arm64/` is
reused as-is. Only the firmware handoff differs: instead of a flattened device
tree in `x0`, the kernel receives a `BootHandoff` that `boot/uefi/` already
built from the UEFI memory map.

Unlike the `-kernel` path, the MMU is already on when the kernel starts here —
EDK2 leaves an identity mapping in place. That happens to be exactly what
`arch/arm64/paging.cpp` assumes, so nothing breaks, but it is the reason this
platform must not enable the MMU a second time.

PSCI is used for power off and restart rather than the UEFI runtime services,
because the kernel has already called `ExitBootServices`.
