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

The repository currently provides the common interfaces, BootInfo contract,
generic startup path, build targets, and a host smoke test. Hardware bring-up
is intentionally incremental; use `docs/architecture.md` for the boundary
rules and next milestones.

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
