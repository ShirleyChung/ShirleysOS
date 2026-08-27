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

The QEMU stage checks more than the greeting: every stage of the interrupt
subsystem has to report itself, the timer has to deliver a full second of IRQ0
interrupts, and on the x86 targets real key events are injected through the
QEMU monitor and must come back echoed by the IRQ1 handler. Input that keeps
working is what proves end-of-interrupt is correct.
