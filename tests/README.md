# Tests

Host tests validate contracts that do not require privileged CPU instructions.
`scripts/test-all.sh` runs them, then boots every emulatable target under QEMU
and checks the guest's own output.

| Test | Covers |
| ---- | ------ |
| `host_smoke.cpp` | Physical page accounting |
| `core_services_smoke.cpp` | Formatting, byte streams, block devices, scheduler |
| `platform_model_smoke.cpp` | E820, device tree, and Apple `boot_args` parsing |
| `boot_loader_smoke.cpp` | ELF reading, UEFI memory map conversion, handoff validation |
| `input_smoke.cpp` | PS/2 scancode decoding and the interrupt input queue |
| `file_system_smoke.cpp` | Bounded string helpers, and mounting, walking, listing, and reading the root file system image |

`file_system_smoke.cpp` runs against the very image that boots: the build packs
`rootfs/` once, and both the kernel and the test link the same bytes.

The QEMU stage boots each target to its shell prompt and then types at it. It
lists the root directory and checks for the entries `rootfs/` really contains,
changes directory and reads a file through a relative path, confirms the prompt
follows the working directory, and reads `uptime` to prove timer interrupts
keep arriving — a count stuck at zero would mean end-of-interrupt is broken.
The last command is `hello`, which hands the CPU to the embedded user program
and does not come back.

On the x86 targets the first line is typed with real key events injected
through the QEMU monitor, so the IRQ1 path is exercised end to end: every
character has to echo and the command has to actually run. ARM64 takes all of
its input over the PL011, which the rest of the typed commands cover.
