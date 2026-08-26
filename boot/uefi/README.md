# ShirleyOS UEFI boot loader

The ShirleyOS boot loader for UEFI firmware: OVMF on x86_64 and EDK2/AAVMF on
ARM64. It is a PE32+ EFI application, not an ELF, and it is the only part of
the project that runs inside the firmware environment.

| File | Responsibility |
| ---- | -------------- |
| `uefi.hpp` | The subset of the UEFI specification this loader calls |
| `main.cpp` | Load the kernel, collect the memory map, exit boot services, hand over |

## What it does

1. Opens the volume it was itself loaded from and reads `\shirley\kernel.elf`.
2. Walks the ELF program headers with `boot/common/elf64.cpp`, claims the
   kernel's physical address range with `AllocatePages(AllocateAddress)`, and
   copies each `PT_LOAD` segment into place, zeroing the `.bss` tail.
3. Records the GOP framebuffer and the ACPI RSDP, when present.
4. Calls `GetMemoryMap` and `ExitBootServices`, retrying when the map key goes
   stale.
5. Converts the UEFI memory map into a neutral `BootInfo` with
   `platform/firmware/uefi.cpp`, marks the kernel image and the handoff block
   as reserved, and jumps to the kernel entry point with a `BootHandoff`.

## Constraints worth knowing

**No allocation after `ExitBootServices`.** Every buffer the handoff needs —
the memory map, the region array, the `BootHandoff` itself — is allocated
before that call. This is why `uefi_memory_map()` writes into a caller-supplied
array rather than allocating one.

**The descriptor stride is not `sizeof`.** Firmware may report a descriptor
larger than `EfiMemoryDescriptor`, so the map is always walked with the
firmware-reported `descriptor_size`.

**Two calling conventions meet here.** UEFI uses the Microsoft x64 convention
while the kernel uses System V, so on x86_64 the kernel entry point is called
through a pointer explicitly marked `sysv_abi`. Without that the handoff
pointer would arrive in RCX and the kernel would read garbage.

**No libc.** `boot/common/runtime.cpp` supplies the `memcpy`/`memset` the
compiler emits implicitly. It is separate from `kernel/runtime/` because that
version halts through the architecture layer, which does not exist here.

## Status

Both `BOOTX64.EFI` and `BOOTAA64.EFI` build, and the logic that can be tested
without firmware — ELF reading, memory map conversion, handoff validation — is
covered by `tests/boot_loader_smoke.cpp`. The firmware-facing path itself has
only been reviewed, not yet booted.
