# Generic interrupt routing

`shirley::irq` is the only interrupt interface a device driver sees. A driver
requests a platform IRQ number and a handler; everything below — which
architecture vector that IRQ lands on, whether the interrupt was spurious, and
how end-of-interrupt is signalled — is asked of the platform layer.

`request()` hooks the architecture vector before it unmasks the IRQ, so an
interrupt can never arrive on a vector that is not wired up yet; `release()`
reverses the order. `dispatch()` skips both the handler and the
end-of-interrupt for a spurious interrupt, and sends the end-of-interrupt even
when no handler is installed, because a controller left in service delivers
nothing further at that priority.

The layer supports both controller shapes. One vector per IRQ, as on the 8259A
or an IOAPIC, is wired up here automatically. A controller that funnels every
device interrupt into a single vector, as a GIC or Apple's AIC does,
demultiplexes in its own driver and calls `dispatch()` with the IRQ it found.
