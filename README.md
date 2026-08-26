# ShirleyOS

ShirleyOS is a research operating system designed around a strict separation
between CPU architecture and hardware platform.

The first targets are QEMU x86_64 and QEMU ARM64. Apple Silicon is modelled as
an ARM64 architecture with a separate `platform/apple_silicon` implementation.

## Layout

* `kernel/` contains architecture- and platform-neutral kernel code.
* `arch/` contains ISA and privilege-level code.
* `platform/` contains machine, firmware, and device integration.
  `platform/pc/` is hardware shared by all PC-compatible machines and
  `platform/firmware/` is firmware data formats shared across machines.
* `libc/` is shared except for syscall trampolines in `libc/arch/`.

## Current status

Milestone M0.5 brings up both architectures for real. x86_64 installs its own
GDT, TSS, and IDT with CPU exception reporting; ARM64 installs the EL1
exception vector table. Both architectures provide a page-table implementation
of the generic address-space interface and a user-mode entry path. Every
platform converts its firmware's memory map — BIOS E820, a flattened device
tree, or Apple `boot_args` — into the neutral `BootInfo` that drives the page
allocator. See [OS_SPEC.md](OS_SPEC.md) for the architectural source of truth
and roadmap.

Booting x86_64 under QEMU prints, from the guest kernel's own serial port:

```text
ShirleyOS booting...
Architecture: x86_64
Processor: GenuineIntel
Platform: QEMU x86_64
Machine: QEMU PC with SeaBIOS firmware
Memory regions: 7
Usable memory: 511 MiB
Free pages: 130870
Interrupts: enabled
Hello! Shirley's OS.
```

## Running ShirleyOS on macOS

Install the host tools explicitly when needed:

```sh
brew install cmake ninja llvm lld qemu
./scripts/check-deps.sh
```

On Apple Silicon, the default target is ARM64. Run it directly with:

```sh
./shirley
./shirley arm64
./shirley x86_64
./shirley test
./shirley debug arm64
./shirley build apple_silicon
```

QEMU is emulating/virtualizing the guest machine. The ShirleyOS boot messages
shown in the terminal are written by the guest kernel through its UART, not by
the shell wrapper. Normal QEMU exit is Ctrl-A then X when using `-nographic`.
The debug scripts pause QEMU and expose a GDB-compatible server on
`localhost:1234`; LLDB can connect with `gdb-remote localhost:1234`.

QEMU has no Apple Silicon machine model, so `apple_silicon` is a build-only
target. Running it on real hardware needs an m1n1-style loader and is
milestone M8.

The current direct QEMU loaders are Development Boot Mode. The production
architecture remains firmware, ShirleyOS bootloader, kernel, and `/bin/init`.

## Build

The default target is the host build, which compiles the architecture-neutral
kernel components and the firmware memory-map parsers so they can be tested on
the development machine:

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Cross targets are exposed when the corresponding freestanding toolchain is
available. Architecture and platform are independent options; `SHIRLEY_TARGET`
is a shorthand for the common pairs:

```sh
cmake -S . -B build/x86_64 -DSHIRLEY_TARGET=x86_64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64.cmake
cmake -S . -B build/arm64 -DSHIRLEY_TARGET=arm64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
cmake -S . -B build/apple -DSHIRLEY_ARCH=arm64 -DSHIRLEY_PLATFORM=apple_silicon \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
```
