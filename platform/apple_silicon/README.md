# Apple Silicon platform

Apple Silicon is ARM64, not a separate architecture, so the ISA work is in
`arch/arm64/` and only machine-specific integration lives here.

| File | Responsibility |
| ---- | -------------- |
| `boot_protocol.cpp` | Reads the Apple `boot_args` structure passed in `x0` |
| `console.cpp` | S5L-derived debug UART, which is not a PL011 |
| `interrupt_controller.cpp` | AIC version 1, as used by M1 (t8103) |
| `platform.cpp` | Machine identity, capabilities, IRQ routing |
| `kernel.ld` | Kernel layout with `.text.boot` first and `.bss` markers |

## Status

This target builds and is reviewable, but it has not been run. QEMU has no
Apple Silicon machine model, so `./shirley` only offers
`build apple_silicon`. Bring-up on real hardware needs an m1n1-style loader
and is milestone M8.

Two addresses are currently compile-time constants taken from the M1 (t8103)
SoC: the debug UART base and the AIC base. On real hardware both come from the
Apple device tree, which is a different format from the flattened device tree
and is not parsed yet. The `boot_args` parsing itself is in
`platform/firmware/` and is covered by host tests.

`interrupt_controller.cpp` is a complete AIC path: it masks, unmasks, and
acknowledges, and it hooks the IRQ exception vector itself so it can read the
EVENT register, work out which device fired, and call `irq::dispatch()`. That
is why `platform::irq_vector()` returns `demultiplexed_vector` here — the IRQ
layer must not hook the vector per IRQ when one vector carries every device.

Two details are worth knowing when this is finally run on hardware. Reading
EVENT consumes an event, so `end_of_interrupt` deliberately does not read it;
only the demultiplexing loop does, and it keeps reading until the controller
reports no event left. And only event type 1, a device IRQ, is dispatched:
IPIs and timer events use other types and are dropped rather than handed to a
driver as though they were IRQs.

None of this has ever executed. QEMU has no Apple Silicon machine model, the
register layout comes from Asahi Linux's published documentation rather than
from a datasheet, and the exact acknowledge semantics need confirming against
real hardware in M8.

Still missing for real hardware: the architected timer, whose interrupt number
comes from the Apple device tree; Apple device tree parsing itself; SMC and
PMU drivers for power off and restart; AIC version 2 for M2 and later SoCs;
and a framebuffer console.
