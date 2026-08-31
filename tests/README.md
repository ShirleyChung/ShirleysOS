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
| `device_smoke.cpp` | The device abstraction, the registry's failure cases, and the console's input multiplexing |
| `vfs_smoke.cpp` | Path normalization, the mount table, open/read/write/seek/close, devfs, and block-level device access |
| `file_system_smoke.cpp` | Bounded string helpers, and mounting, walking, listing, and reading the root file system image |

`file_system_smoke.cpp` and `vfs_smoke.cpp` run against the image the kernel
mounts: the build packs `rootfs/` once and both the kernel and the tests link
the same bytes. A kernel target additionally packs the user program it just
built in as `/bin/hello`, which the host build has no cross toolchain to
produce; that path is covered by the QEMU stage instead.

The QEMU stage boots each target to its shell prompt and then types at it. It
lists the root directory and checks for the entries `rootfs/` really contains,
changes directory and reads a file through a relative path, confirms the prompt
follows the working directory, and reads `uptime` to prove timer interrupts
keep arriving — a count stuck at zero would mean end-of-interrupt is broken.

It also checks the VFS from the outside. `ls /` has to show `dev`, which exists
in no file system and can only come from the mount table; `ls /dev` has to show
the devices the boot log listed; and `blk /dev/ram0 0` has to print the SHRFS1
header, which proves an `open()` followed by a `block_read()` reached the actual
sectors of the disk the root file system is mounted from.

The write path is checked the same way round. `echo ... > /dev/uart0` has to
appear in the guest's serial output, which means `vfs::write()` reached the UART
driver; `> /dev/null` must swallow it; and `> /etc/version` must be refused at
`open()` because SHRFS1 is read-only.

The last command is `exec /bin/hello`, which reads a program out of the file
system and hands it to the ELF loader. It takes over the CPU and does not come
back.

On the x86 targets the first line is typed with real key events injected
through the QEMU monitor, so the IRQ1 path is exercised end to end: every
character has to echo and the command has to actually run. ARM64 takes all of
its input over the PL011, which the rest of the typed commands cover.
