# Firmware data formats

Formats that firmware uses to describe a machine to the kernel. A format is
not a machine: BIOS E820 is used by every PC, flattened device trees are used
by QEMU `virt`, U-Boot, and UEFI, and Apple `boot_args` is used by both iBoot
and m1n1.

Everything here is a pure data transformation with no hardware access, so it
compiles into the host build and is covered by
`tests/platform_model_smoke.cpp`. Firmware data is untrusted input: every
parser bounds-checks the blob rather than assuming it is well formed.

| File | Format |
| ---- | ------ |
| `e820.cpp` | BIOS INT 15h E820 memory map |
| `fdt.cpp` | Flattened device tree: `/memory` node and reservation block |
| `apple_boot_args.cpp` | Apple `boot_args`: memory size, framebuffer, device tree |
