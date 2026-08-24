# ShirleyOS

ShirleyOS is a research operating system designed around a strict separation
between CPU architecture and hardware platform.

The first targets are QEMU x86_64 and QEMU ARM64. Apple Silicon is modelled as
an ARM64 architecture with a separate `platform/apple_silicon` implementation.

## Layout

* `kernel/` contains architecture- and platform-neutral kernel code.
* `arch/` contains ISA and privilege-level code.
* `platform/` contains machine, firmware, and device integration.
* `libc/` is shared except for syscall trampolines in `libc/arch/`.

## Current status

Milestone M0 provides real freestanding ARM64 and x86_64 guest kernels,
platform-isolated serial consoles, direct development boot under QEMU, and
an automated serial-output integration test. See [OS_SPEC.md](OS_SPEC.md) for
the architectural source of truth and roadmap.

## Running ShirleyOS on macOS

Install the host tools explicitly when needed:

```sh
brew install cmake ninja llvm qemu
./scripts/check-deps.sh
```

On Apple Silicon, the default target is ARM64. Run it directly with:

```sh
./shirley
./shirley arm64
./shirley x86_64
./shirley test
./shirley debug arm64
```

QEMU is emulating/virtualizing the guest machine. The ShirleyOS boot messages
shown in the terminal are written by the guest kernel through its UART, not by
the shell wrapper. Normal QEMU exit is Ctrl-A then X when using `-nographic`.
The debug scripts pause QEMU and expose a GDB-compatible server on
`localhost:1234`; LLDB can connect with `gdb-remote localhost:1234`.

The current direct QEMU loaders are Development Boot Mode. The production
architecture remains firmware, ShirleyOS bootloader, kernel, and `/bin/init`.

## Build

```powershell
cmake -S . -B build
cmake --build build --target shirley-host-smoke
ctest --test-dir build --output-on-failure
```

Cross targets are exposed when the corresponding freestanding toolchain is
available:

```text
shirley-x86_64
shirley-arm64
```
