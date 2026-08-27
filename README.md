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
* `boot/` contains the boot loaders. `boot/uefi/` is the ShirleyOS UEFI loader
  and `boot/common/` is the code every loader needs.
* `libc/` is shared except for syscall trampolines in `libc/arch/`.

## Current status

Milestone M0.5 brings up both architectures for real. x86_64 installs its own
GDT, TSS, and IDT with CPU exception reporting, and drives real device
interrupts through it; ARM64 installs the EL1 exception vector table. Both architectures provide a page-table implementation
of the generic address-space interface and a user-mode entry path. Every
platform converts its firmware's memory map — BIOS E820, a flattened device
tree, Apple `boot_args`, or a UEFI memory map — into the neutral `BootInfo`
that drives the page allocator.

`boot/uefi/` is the ShirleyOS UEFI boot loader: a PE32+ EFI application that
runs under OVMF on x86_64 and EDK2/AAVMF on ARM64, loads the kernel ELF, exits
boot services, and hands over a validated `BootHandoff`. That makes
`x86_64_uefi` and `arm64_uefi` the first targets that boot the way the
production architecture intends, rather than through a development loader.

x86_64 also has a working interrupt subsystem end to end: a 256-entry IDT, the
8259A as its bring-up interrupt controller backend, a generic `shirley::irq`
layer that device drivers use instead of touching a controller, a 100 Hz PIT on
IRQ0, and an interrupt-driven PS/2 keyboard on IRQ1 whose characters are echoed
to the console and queued as standard input. The kernel idles in `hlt` and
polls nothing.

ARM64 now has the same subsystem behind the same `shirley::irq` interface. Its
controllers demultiplex rather than giving each IRQ its own vector: every
device interrupt arrives on the one IRQ exception entry, and the controller
driver identifies the source and dispatches it. `platform/arm/` holds what ARM
defines rather than any one machine — a GICv2 driver and the architected timer
on PPI 30, the ARM counterpart of `platform/pc/` — and `qemu_arm64` and
`qemu_arm64_uefi` both run a 100 Hz timer through it.

Apple Silicon uses its own AIC in `platform/apple_silicon/` instead, which is
now a complete path rather than just register access. It has still never been
executed: QEMU has no Apple Silicon machine model, so that target is built and
reviewed but not booted, and its register layout comes from Asahi Linux's
published documentation rather than a datasheet.

See [OS_SPEC.md](OS_SPEC.md) for the architectural source of truth and
roadmap.

Booting x86_64 under QEMU prints, from the guest kernel's own serial port:

```text
[IRQ] IDT initialized
[IRQ] PIC remapped 0x20/0x28
[IRQ] PIT timer enabled on IRQ0
[IRQ] keyboard IRQ enabled
ShirleyOS booting...
Architecture: x86_64
Processor: GenuineIntel
Platform: QEMU x86_64
Machine: QEMU PC with SeaBIOS firmware
Memory regions: 7
Usable memory: 511 MiB
Free pages: 130870
Interrupts: enabled
Timer: 100 Hz
Keyboard: type to echo through the interrupt path
Hello! Shirley's OS.
[IRQ] timer ticking: 100 interrupts in the first second
```

The memory figures move as the kernel image grows. Typing in the QEMU display
window echoes characters through the IRQ1 path; QEMU sources keyboard events
from its display device, so `./shirley x86_64` opens one by default. Set
`SHIRLEY_HEADLESS=1` for the old `-nographic` behaviour, which is output-only
and has no keyboard.

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
./shirley x86_64_uefi
./shirley arm64_uefi
./shirley test
./shirley debug arm64
./shirley build apple_silicon
```

The UEFI targets need a firmware image. One ships with QEMU, and
`scripts/find-uefi-firmware.sh` locates it; set `SHIRLEY_UEFI_FIRMWARE` to
override the choice.

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
