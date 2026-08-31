# ARM shared hardware

Hardware that ARM defines rather than any one machine, shared by every ARM64
platform that has it. This is the ARM counterpart of `platform/pc/`: it is not
a machine directory, so nothing here reads a machine's identity.

| File | Device |
| ---- | ------ |
| `gicv2.cpp` | Generic Interrupt Controller version 2: distributor and CPU interface |
| `generic_timer.cpp` | ARM architected timer on PPI 30, at 100 Hz |
| `pl011_input.cpp` | PL011 UART receive path and the `uart0` device, the console's only input on these machines |

## GICv2

A GICv2 demultiplexes. Every device interrupt reaches the core on the single
IRQ exception entry, so the driver hooks that vector itself, reads `GICC_IAR`
to learn which interrupt actually fired, and calls `irq::dispatch()` with it.
That is also why `platform::irq_vector()` returns `demultiplexed_vector` on
these platforms: if the IRQ layer hooked the vector per IRQ instead, every
driver would land on the same vector and overwrite both each other and this
handler.

Acknowledging continues until `GICC_IAR` returns a reserved number — 1023
means nothing is pending — because one exception can cover several pending
interrupts. A reserved number is never given an end-of-interrupt.

The distributor starts with every interrupt masked and every pending state
cleared, all interrupts at one middle priority, and the CPU interface's
priority mask set looser than that so interrupts pass at all. SPIs are
configured level-triggered and targeted at the boot core; SGIs and PPIs are
private to a core, so their target field is read-only.

`gicv2_end_of_interrupt()` writes the bare interrupt number. An SGI's
end-of-interrupt also carries its source core, which will matter when
inter-processor interrupts arrive with SMP; there are no SGIs while there is
one core.

## PL011 input

The platform's console backend writes through the PL011; this is the other
half, turning received characters into console input. Both the receive and the
receive-timeout interrupts are unmasked, and the FIFO level is set to one
eighth: a terminal sends a single character at a time, well below any higher
trigger level, and without the timeout interrupt those keystrokes would sit in
the FIFO forever. The handler drains until the FIFO is empty, because a
leftover byte keeps the interrupt pending, and it does nothing but push into
its ring buffer.

That buffer is what the `uart0` device reads from, and the device is attached
to the console once the IRQ is really live. `uart0` can be written to as well,
through the same registers; the console backend is not shared for that because
a backend belongs to its platform while this driver is common to every PL011
machine.

## Architected timer

The timer is part of the CPU rather than the machine, driven through
`CNTP_*_EL0`, so every ARM64 machine has one. `CNTFRQ_EL0` gives the counter
rate, and the interval is derived from it, so the reported frequency is what
the hardware really runs at rather than what was asked for.

Its interrupt output is a level, not a pulse. The handler therefore rearms
`CNTP_TVAL_EL0` first: leaving the condition met would re-enter the handler
forever.

## Not here yet

The GIC addresses are compile-time constants for QEMU virt, the same approach
`platform/apple_silicon` takes for the AIC base. Reading them from the device
tree's interrupt controller node needs generic node lookup in
`platform/firmware/fdt.cpp`, which only understands `/memory` today.

GICv3, its redistributors and system-register CPU interface, and the GICv2m
and ITS paths that carry MSIs on ARM64 are all still to come. Apple Silicon
uses its own AIC in `platform/apple_silicon/` and none of this directory.
