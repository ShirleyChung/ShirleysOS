# ShirleyOS Specification

## Goals

ShirleyOS is designed to run everywhere, remain stable, be easy to maintain,
and deliver high efficiency.

## CPU architectures and platform model

Current CPU architectures are x86_64 and ARM64; RISC-V64 is planned. CPU
architecture and machine platform are separate concepts:

```text
arch/     x86_64/  arm64/
platform/ qemu_x86_64/  pc/  qemu_arm64/  apple_silicon/
```

Apple Silicon is an ARM64 platform, not a CPU architecture. Adding an
architecture should primarily add `arch/<architecture>/`; adding a platform
should primarily add `platform/<platform>/`. Thus Apple Silicon is
`arch/arm64/` plus `platform/apple_silicon/`.

## Current development environment

```text
Apple Silicon Mac -> macOS -> Clang/LLVM -> QEMU -> ShirleyOS ARM64/x86_64
```

No physical target hardware is required. Tier 1 reference platforms are QEMU
ARM64 `virt` and QEMU x86_64. Future Tier 2 platforms are Apple Silicon and a
standard x86_64 UEFI PC.

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

## Boot architecture

The long-term production path is:

```text
Firmware -> ShirleyOS Bootloader -> ShirleyOS Kernel -> /bin/init
```

### Development Boot Mode

Milestone M0 permits direct QEMU kernel loading for rapid development. The
current ARM64 path uses QEMU `virt -kernel`; x86_64 uses a small BIOS disk
loader that enters long mode and loads the kernel. These are development
launch mechanisms, not a permanent production boot dependency.

## Milestones

### M0 — QEMU boot and console

The current milestone makes ShirleyOS genuinely bootable and observable from a
macOS Terminal. `./shirley` builds, launches QEMU, attaches the guest serial
console, and shows output from the guest kernel:

```text
ShirleyOS booting...
Architecture: ARM64
Hello! Shirley's OS.
```

The equivalent x86_64 target is required. The wrapper must never fake guest
output.

### M1 — memory and userspace

Implement bootloader handoff, physical and virtual memory managers, heap, ELF
loader, userspace, minimal libc, C `main()`, `printf()`, and syscalls. The
shared `kernel/`, `libc/`, and `user/hello/main.c` sources must support both
current architectures; ISA-specific work stays under `arch/`. The hello C
program must actually execute in userspace.

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
