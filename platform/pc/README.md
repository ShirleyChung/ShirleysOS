# PC platform hardware

Hardware that every IBM-PC-compatible machine has, shared by the QEMU PC
targets and by future physical PC support. Machine-specific integration lives
in the individual platform directories instead.

| File | Device |
| ---- | ------ |
| `pic.cpp` | 8259A interrupt controller, remapped so IRQ *n* arrives on vector 32+*n* |
| `pit.cpp` | 8253/8254 interval timer on IRQ0, programmed as a 100 Hz rate generator |
| `ps2_keyboard.cpp` | 8042 controller and PS/2 keyboard on IRQ1 |
| `serial_console.cpp` | COM1 serial console output |
| `serial_input.cpp` | COM1 receive path on IRQ4, so a serial terminal can type at the console |

The 8259A is the bring-up interrupt controller backend, not the long-term one:
it needs no ACPI and no enumeration, which is what makes it the right way to
get the interrupt path working end to end. A local APIC and IOAPIC backend,
and the MSI/MSI-X path that follows from it, replace it behind the unchanged
`shirley::irq` interface.

`ps2_keyboard.cpp` only reads ports and drives IRQ1; the scancode translation
it uses is hardware-independent and lives in `drivers/input/scancode.cpp`
where tests can reach it.

A PC has two console input devices, and both push into the one queue
`io::console_input()` owns: the keyboard, and whatever terminal is on the
other end of COM1. The shell reads that queue and never learns which of them a
character came from. Neither driver echoes; that belongs to the line editor.

The two sides of COM1 share their register definitions through
`shirley/platform/pc/serial.hpp`. The input side lowers the receive FIFO's
trigger level to one byte: a terminal sends one character at a time, and the
fourteen-byte level the output side leaves behind would hold keystrokes in the
FIFO waiting for an interrupt that never comes.

A full PC platform directory — UEFI entry, ACPI tables, local APIC — is
milestone M7.
