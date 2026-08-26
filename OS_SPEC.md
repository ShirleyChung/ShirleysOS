# ShirleyOS Project Specification

This document is the canonical source of truth for ShirleyOS architecture,
product direction, supported targets, and milestone intent. Implementation and
secondary documentation must conform to it.

## 1. Vision and guiding principles

ShirleyOS will evolve from a bootable research kernel into an installable,
self-hosting, general-purpose operating system—not merely a hobby kernel or
QEMU demonstration. Its primary goals are:

1. Run everywhere.
2. Remain stable.
3. Be easy to maintain.
4. Achieve high efficiency.
5. Be portable across CPU architectures and hardware platforms.
6. Eventually host its own native development toolchain.

The long-term user experience is:

```text
ShirleyOS booting...

$ cat hello.c
#include <stdio.h>

int main(void)
{
    printf("Hello from ShirleyOS!\n");
    return 0;
}

$ gcc hello.c -o hello
$ ./hello

Hello from ShirleyOS!
```

This requires real-hardware boot, a shell, persistent storage, process
isolation, practical libc/POSIX support, local C compilation, and execution of
the resulting binary.

> Portable at the boundaries, simple in the core, efficient on the hot path.

> Architecture differences belong in `arch/`.

> Machine/platform differences belong in `platform/`.

> Generic kernel code should not care whether it runs on x86_64, ARM64, QEMU,
> a PC, or Apple Silicon.

> POSIX compatibility is a userspace compatibility goal, not the internal
> kernel architecture.

> First make it correct and testable, then make it fast.

> ShirleyOS should evolve from a bootable research kernel into an installable,
> self-hosting general-purpose operating system.

## 2. CPU architectures and hardware platforms

A CPU architecture defines instruction-set and privilege mechanisms. A
platform defines a machine's firmware, buses, interrupt controllers, timers,
and devices. Supported CPU architectures are x86_64 and ARM64; RISC-V64 is a
future architecture.

```text
arch/
|-- x86_64/
`-- arm64/

platform/
|-- qemu_x86_64/
|-- pc/
|-- qemu_arm64/
`-- apple_silicon/
```

Apple Silicon is an ARM64 platform, not an architecture, and therefore uses
`arch/arm64/` plus `platform/apple_silicon/`. Generic ARM64 code must contain
no Apple-specific device logic. Generic kernel code must contain no QEMU-, PC-,
or Apple-specific assumptions. The existing generic console boundary,
`shirley::console::{initialize,write}`, exemplifies this separation.

### Fundamental portability rule

Adding a CPU architecture should primarily require
`arch/<new_architecture>/`; adding a machine for an existing architecture
should primarily require `platform/<new_platform>/`. Adding ARM64 must not
rewrite the generic kernel. Adding Apple Silicon must not rewrite generic ARM64
EL0/EL1 handling, MMU abstractions, translation tables, exception handling, or
syscall support. Apple-specific firmware and hardware belong in platform and
device layers.

## 3. Development and target environments

Physical hardware is not required initially. The primary host path is:

```text
Apple Silicon Mac
        |
        v
macOS
        |
        v
LLVM / Clang / LLD
        |
        v
QEMU
        |
        v
ShirleyOS
```

QEMU ARM64 and QEMU x86_64 are the reference targets. Developers should be
able to run `./shirley`, `./shirley arm64`, or `./shirley x86_64` and see real
guest-kernel serial output in the macOS terminal. Shell scripts must never
simulate guest output.

The primary long-term physical targets are standard Intel/AMD x86_64 UEFI PCs
and supported Apple Silicon Macs:

```text
Firmware
   |
   v
ShirleyOS Bootloader
   |
   v
ShirleyOS Kernel
   |
   v
Storage Driver
   |
   v
Root Filesystem
   |
   v
/sbin/init
   |
   v
Shell
```

UEFI is the primary x86_64 firmware target; legacy BIOS is not a priority.
Apple Silicon needs its own bring-up and boot strategy without contaminating
generic ARM64 code.

## 4. Modular hybrid kernel

ShirleyOS uses a modular hybrid-kernel design, not a strict pure microkernel.

```text
Applications
--------------------------------
POSIX Compatibility / libc
--------------------------------
ShirleyOS Native ABI
--------------------------------
Generic Kernel
--------------------------------
Architecture Abstraction
--------------------------------
Platform Abstraction
--------------------------------
Hardware
```

The generic kernel will contain virtual and physical memory management, a
scheduler, threads, processes, IPC, handles/capabilities, a minimal VFS core, a
device model, and a syscall dispatcher. Performance-critical facilities may
run in kernel space; facilities for which isolation matters more may later run
in userspace.

## 5. Boot architecture and BootInfo

The production path is:

```text
Firmware -> ShirleyOS Bootloader -> Kernel ELF -> BootInfo -> kernel_main()
```

The bootloader/kernel interface uses a versioned, architecture-neutral
`BootInfo`. It normalizes firmware/platform data into ShirleyOS-owned
structures, eventually including a memory map, framebuffer information,
device-tree pointer where applicable, firmware information, and boot modules or
initramfs. Generic startup consumes this contract, not firmware-native data.

### Development Boot Mode

Early QEMU work may directly load a kernel to simplify bring-up. The current
ARM64 path uses QEMU `virt -kernel`; the x86_64 path uses a small BIOS disk
loader that enters long mode and loads the kernel. These are Development Boot
Mode mechanisms, not permanent architectural dependencies or substitutes for
the production bootloader and BootInfo contract.

## 6. Memory management

ShirleyOS must provide a physical memory manager, virtual memory manager,
kernel heap, userspace address spaces, and shared memory. The initial page size
is 4 KiB. The first physical allocator may use a bitmap, but implementations
must remain replaceable behind generic memory interfaces.

Architecture-specific VM machinery stays isolated: x86_64 uses PML4, CR3, and
x86 page tables; ARM64 uses translation tables, TTBR, TCR, and MAIR. Generic
kernel code must not manipulate these directly.

## 7. Userspace, processes, and ELF

M1 may execute one userspace program, but the long-term model is:

```text
Process
|-- AddressSpace
|-- HandleTable
|-- Threads
|-- Credentials
`-- Resources
```

Userspace executes at x86_64 Ring 3 or ARM64 EL0; the kernel executes at
x86_64 Ring 0 or ARM64 EL1. Calling a program's `main()` inside the kernel is
not userspace execution and must not be used as a substitute.

The primary executable format is ELF64. Initial support covers ET_EXEC, static
binaries, and PT_LOAD, with no dynamic linker or shared libraries. The loader
validates EM_X86_64 and EM_AARCH64. ELF parsing should be generic wherever
possible.

## 8. Native ABI and syscalls

ShirleyOS exposes a stable native userspace ABI. Kernel APIs may evolve, while
userspace ABI compatibility should be retained whenever practical. Syscall
semantics and dispatch are architecture-independent even though entry differs:

```text
x86_64: SYSCALL --+
                  +--> shared generic syscall dispatcher
ARM64:  SVC -----+
```

The bootstrap syscall set may contain only `write` and `exit`, then expand as
needed. All user pointers and lengths crossing the ABI must be validated.

## 9. POSIX compatibility and libc

POSIX is a major userspace interface goal, not a mandate for Unix kernel
internals. ShirleyOS retains native process, handle, IPC, and resource models
while incrementally exposing Unix-compatible behavior, eventually including:

```text
open close read write lseek
fork execve wait
pipe dup dup2
mmap munmap brk
stat fstat
chdir getcwd
signals time pthread primitives
```

Early milestones may use a minimal ShirleyOS libc with `write`, `_exit`,
`puts`, `putchar`, `printf`, `strlen`, `memcpy`, `memmove`, `memset`, and
`memcmp`. This bootstrap libc is not the production libc. The long-term goal is
to port musl, and native ABI design must make a standard libc port practical.

## 10. Toolchain and self-hosting

### Stage A: cross compilation

Initially macOS or Linux builds ShirleyOS ELF programs:

```text
macOS/Linux -> cross compiler -> ShirleyOS ELF -> ShirleyOS
```

Possible driver names are `x86_64-shirley-gcc` and
`aarch64-shirley-gcc`. LLVM/Clang is appropriate for early bootstrap; GCC need
not be the first compiler used during OS bring-up.

### Stage B: native compilation

Eventually the compiler runs inside ShirleyOS:

```bash
$ gcc hello.c -o hello
$ ./hello
Hello from ShirleyOS!
```

The native environment must include a C compiler, assembler, linker, `ar`, and
`make` or equivalent. A possible route is LLVM/Clang cross toolchain ->
ShirleyOS userspace -> musl -> binutils/GCC port -> native GCC. An equivalent
toolchain is acceptable if it fulfills native development.

## 11. Persistent storage and filesystem

Persistent storage is required:

```text
block device layer -> filesystem -> VFS -> file descriptors -> userspace
```

VirtIO Block is the initial QEMU storage target; NVMe is the initial x86_64
physical target. The first on-disk filesystem may be simple but must be well
documented. Prefer an existing simple filesystem such as ext2 before inventing
a custom one. Mature support includes directories, regular files, mounting,
permissions, and a persistent root filesystem. Initramfs alone does not satisfy
the persistence requirement.

## 12. Shell and user environment

ShirleyOS should boot through `/sbin/init` into a usable shell. Minimal native
tools or ports are both acceptable during bring-up. The target experience is:

```text
ShirleyOS

$ pwd
/home/user
$ ls
hello.c
$ gcc hello.c -o hello
$ ./hello
Hello from ShirleyOS!
```

## 13. Device and driver model

```text
Device
|-- Bus
|   |-- PCI
|   `-- USB
`-- Driver
```

Drivers use defined kernel interfaces rather than arbitrary hardware access in
generic code. They may eventually run in kernel space or userspace according
to performance and reliability needs. Initial virtual devices should favor
VirtIO, PL011, and COM1; real hardware support is added incrementally.

## 14. Efficiency, stability, and security

Hot paths should avoid unnecessary copying, context switches, global locks,
and hidden allocations. Prefer page sharing where useful and support future
zero-copy IPC. Portability abstractions must not impose avoidable hot-path
overhead.

Long-term policy requires userspace isolation, memory protection, a small
trusted core, strict user-pointer validation, clear panic behavior, driver
isolation where practical, a stable userspace ABI, and memory-safe languages
where beneficial. Resources should move toward a capability-oriented handle
model, including file, socket, shared-memory, process, and device handles.

## 15. Language policy

```text
Kernel core       C++20
Architecture      C++20 + assembly
Bootloader        C/C++ + assembly
Drivers           C++20 / Rust
System services   Rust / C++
libc              C
Userspace         C / C++ / Rust and others later
```

Kernel C++ is freestanding with `-fno-exceptions` and `-fno-rtti`, has no
hosted-runtime dependency, avoids uncontrolled global initialization, and uses
explicit allocation. Rust may be introduced where memory safety has concrete
value.

## 16. Repository architecture

```text
/
|-- OS_SPEC.md
|-- README.md
|-- boot/
|-- kernel/
|-- arch/
|   |-- x86_64/
|   `-- arm64/
|-- platform/
|   |-- qemu_x86_64/
|   |-- qemu_arm64/
|   |-- pc/
|   `-- apple_silicon/
|-- drivers/
|-- libc/
|-- user/
|-- include/
|-- tests/
|-- scripts/
`-- docs/
```

Generic kernel implementations must not be duplicated between architectures.
Shared libc and userspace remain neutral except for necessary ABI trampolines.

## 17. Milestone roadmap

Numbering may evolve, but dependency order and intent must remain clear.

- **M0:** QEMU boot and real serial console for x86_64 and ARM64.
- **M1:** Physical/virtual memory, kernel heap, syscalls, ELF loader, true
  userspace, minimal libc, and `hello.c`.
- **M2:** Interrupts, timer, threads, and scheduler.
- **M3:** Process model, `exec`, `wait`, pipes, and IPC.
- **M4:** VFS, initramfs, persistent filesystem, and file descriptors.
- **M5:** VirtIO Block, basic storage stack, shell, and basic utilities.
- **M6:** musl libc port and broader POSIX compatibility.
- **M7:** Host cross-development toolchain for ordinary ShirleyOS C programs.
- **M8:** Run increasingly normal Unix software.
- **M9:** x86_64 PC bring-up, UEFI installation, NVMe, and persistent root.
- **M10:** Apple Silicon platform bring-up without ARM64-layer leakage.
- **M11:** binutils/GCC or equivalent native toolchain port.
- **M12:** Compile and execute C entirely inside ShirleyOS.

Networking, graphics, and other subsystems may be scheduled as dependencies
and product needs become clear; they do not replace this sequence.

## 18. Installation goal

ShirleyOS must eventually be installable. The standard x86_64 PC flow is:

```text
installer media
   |
   v
UEFI boot
   |
   v
partition target storage
   |
   v
EFI System Partition
   |
   v
ShirleyOS bootloader
   |
   v
root filesystem
```

Apple Silicon installation will use the boot architecture appropriate to that
platform and must not be assumed to follow the PC UEFI process.

## 19. Definition of long-term success

ShirleyOS achieves its primary general-purpose OS goal when it can:

- boot under QEMU x86_64;
- boot under QEMU ARM64;
- install and boot on a supported x86_64 UEFI PC;
- install and boot on supported Apple Silicon hardware;
- provide persistent storage;
- provide process isolation and a shell;
- provide useful POSIX compatibility and run a standard libc;
- cross-compile ordinary C programs for ShirleyOS;
- eventually run a compiler natively;
- compile a C source file inside ShirleyOS; and
- execute the resulting program.

The final demonstration on supported real hardware is:

```bash
$ gcc hello.c -o hello
$ ./hello
Hello from ShirleyOS!
```

## 20. Immediate development goal

The immediate sequence remains:

```text
macOS -> ./shirley -> QEMU -> ShirleyOS guest kernel -> serial console
```

Then:

```text
memory management -> userspace -> minimal libc -> C hello program
```

Do not prematurely implement GCC, NVMe, an installer, or physical Apple
Silicon support before the portable kernel foundations work and are testable on
both QEMU reference targets.

## 21. Specification maintenance

Any implementation change to architecture, platform abstraction, boot
protocol, `BootInfo`, ABI, syscall interface, memory model, process model,
filesystem, driver model, supported hardware, toolchain strategy, or milestone
status must update this document in the same change.

Detailed documentation may live elsewhere, but it cannot override or
contradict this specification. Implementation and specification must never
silently diverge.
