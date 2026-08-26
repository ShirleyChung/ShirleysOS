# ShirleyOS Specification

## Goals

ShirleyOS is designed to run everywhere, remain stable, be easy to maintain,
and deliver high efficiency.

## CPU architectures and platform model

Current CPU architectures are x86_64 and ARM64; RISC-V64 is planned. CPU
architecture and machine platform are separate concepts:

```text
arch/     x86_64/  arm64/
platform/ qemu_x86_64/  pc/  qemu_arm64/  apple_silicon/  firmware/
```

Apple Silicon is an ARM64 platform, not a CPU architecture. Adding an
architecture should primarily add `arch/<architecture>/`; adding a platform
should primarily add `platform/<platform>/`. Thus Apple Silicon is
`arch/arm64/` plus `platform/apple_silicon/`.

The build treats the two axes independently. `SHIRLEY_ARCH` and
`SHIRLEY_PLATFORM` select them; `SHIRLEY_TARGET` is only a shorthand for the
common pairs:

| `SHIRLEY_TARGET` | `SHIRLEY_ARCH` | `SHIRLEY_PLATFORM` |
| ---------------- | -------------- | ------------------ |
| `host`           | `host`         | `host`             |
| `x86_64`         | `x86_64`       | `qemu_x86_64`      |
| `arm64`          | `arm64`        | `qemu_arm64`       |
| `apple_silicon`  | `arm64`        | `apple_silicon`    |

Two `platform/` directories are not machines:

* `platform/pc/` holds hardware shared by every IBM-PC-compatible machine,
  such as the 8259A interrupt controller.
* `platform/firmware/` holds firmware *data formats* that more than one
  machine can present: BIOS E820 memory maps, flattened device trees, and
  Apple `boot_args`. These are pure data transformations with no hardware
  access, so they are compiled into the host build and covered by tests.

### Architecture abstraction

`include/shirley/arch.hpp` is the whole contract between generic kernel code
and an ISA: CPU initialization and identification, interrupt enable/disable,
idle and halt, stack pointer, address-space switching, the transition to user
mode, and interrupt-handler registration.

Architecture-specific headers that platform code legitimately needs live in
`include/shirley/arch/<architecture>/`. They may only be included by that
architecture's own code and by platforms built for it — never by generic
kernel code. Today these are x86_64 port I/O and page tables, and the ARM64
exception-vector numbering and page tables.

`register_interrupt_handler()` takes an architecture interrupt vector, and
the vector space is defined per architecture:

* x86_64: IDT vectors 0-255. Vectors 0-31 are CPU exceptions; the platform
  interrupt controller is remapped so device IRQ *n* arrives on vector 32+*n*.
* ARM64: the 16 EL1 exception-vector entries from
  `include/shirley/arch/arm64/exception.hpp`. Device interrupts all arrive on
  an IRQ entry, and the interrupt-controller driver demultiplexes them.

Either way, an exception with no registered handler prints a register dump on
the console and stops the processor rather than continuing silently.

### Platform abstraction

`include/shirley/platform.hpp` covers machine identity, a `Capabilities`
record so generic code never tests for a machine by name, interrupt-controller
masking and end-of-interrupt, the platform IRQ to architecture vector mapping,
and power off/restart.

Each platform also supplies `shirley_platform_boot_info()`, which converts
whatever the firmware handed the kernel into the neutral `BootInfo` in
`include/shirley/boot_info.hpp`.

## Current development environment

```text
Apple Silicon Mac -> macOS -> Clang/LLVM -> QEMU -> ShirleyOS ARM64/x86_64
```

No physical target hardware is required for Tier 1. Tier 1 reference platforms
are QEMU ARM64 `virt` and QEMU x86_64. Tier 2 platforms are Apple Silicon and
a standard x86_64 UEFI PC. QEMU has no Apple Silicon machine model, so the
`apple_silicon` target is built and reviewed but not booted; running it on
real hardware needs an m1n1-style loader and belongs to M8.

## Kernel layering

```text
Applications
libc / POSIX Compatibility
ShirleyOS Native ABI
Generic Kernel
Architecture Abstraction
Platform Abstraction
Hardware
```

Generic kernel code must not assume x86_64, ARM64, QEMU, or Apple Silicon.
Architecture differences belong in `arch/`; machine and device differences
belong in `platform/`. POSIX compatibility is an interface goal, not the
internal kernel architecture. The console boundary is
`shirley::console::{initialize,write}`.

The kernel is freestanding. `kernel/freestanding/` supplies the minimal subset
of standard headers the kernel actually uses, and `kernel/runtime/` supplies
the routines the compiler emits implicitly (`memset`, `memcpy`, `memmove`,
`memcmp`, `operator delete`, `__cxa_pure_virtual`). There is no heap, so every
`operator delete` stops the processor instead of pretending to free memory.

## Boot architecture

The long-term production path is:

```text
Firmware -> ShirleyOS Bootloader -> ShirleyOS Kernel -> /bin/init
```

### Development Boot Mode

Milestone M0 permits direct QEMU kernel loading for rapid development.

* ARM64 uses QEMU `virt -kernel`. The firmware passes a flattened device tree
  in `x0`; `platform/qemu_arm64` reads its `/memory` node and reservation
  block.
* x86_64 uses a 512-byte BIOS boot sector in `arch/x86_64/boot.S`. It collects
  the E820 memory map, reads the kernel, identity-maps the first 1 GiB with
  2 MiB pages, enters long mode, and jumps to the kernel with the memory map
  address in `RDI`.
* Apple Silicon expects `x0` to hold an Apple `boot_args` structure, the same
  contract iBoot and m1n1 use.

These are development launch mechanisms, not a permanent production boot
dependency.

### Boot protocol contract

Every platform provides:

```c
const shirley::BootInfo* shirley_platform_boot_info(const void* firmware_table);
```

The architecture entry code zeroes `.bss`, establishes the boot stack, calls
this function with the firmware pointer, and passes the result to
`kernel_main()`. The kernel image itself, the firmware data, and any live
firmware tables are reported as non-usable so the page allocator can never
hand them out.

## Milestones

### M0 — QEMU boot and console

Complete for both Tier 1 targets. `./shirley` builds, launches QEMU, attaches
the guest serial console, and shows output produced by the guest kernel. The
wrapper must never fake guest output.

### M0.5 — architecture and platform bring-up

Current milestone. Both architectures install real CPU state: x86_64 sets up
its own GDT, TSS, and a 256-entry IDT with exception reporting; ARM64 installs
the EL1 exception vector table. Both provide page-table implementations of
`memory::AddressSpace`, a user-mode entry path, and CPU identification. Every
platform supplies a boot protocol and a real memory map, so the page allocator
runs on genuine firmware data rather than a hardcoded region.

Not yet done in this milestone: the ARM64 MMU is implemented
(`arch::arm64::mmu_enable`) but not enabled at boot, and no timer or
interrupt-controller demultiplexing driver exists yet, so all device IRQs stay
masked.

### M1 — memory and userspace

Implement bootloader handoff, virtual memory activation on both
architectures, heap, ELF loader, userspace, minimal libc, C `main()`,
`printf()`, and syscalls. The shared `kernel/`, `libc/`, and
`user/hello/main.c` sources must support both current architectures;
ISA-specific work stays under `arch/`. The hello C program must actually
execute in userspace.

### Roadmap

M2 interrupts, timer, scheduler, and threads; M3 processes and IPC; M4 VFS,
initramfs, and shell; M5 PCI/VirtIO and storage; M6 networking; M7 physical
x86_64 PC bring-up; M8 Apple Silicon bring-up; M9 GUI/window system.

## Coding philosophy

Portable at the boundaries, simple in the core, efficient on the hot path.
Prefer simple, testable implementations before optimization. Generic kernel
code should not care whether it runs on x86_64, ARM64, QEMU, PC, or Apple
Silicon.

## Specification maintenance

Changes to architecture, ABI, boot protocol, memory model, platform
abstraction, syscalls, driver model, supported targets, or milestone status
must update this specification in the same change. Implementation and
specification must not silently diverge.
