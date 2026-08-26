# PC platform hardware

Hardware that every IBM-PC-compatible machine has, shared by the QEMU PC
target and by future physical PC support. Machine-specific integration lives
in the individual platform directories instead.

| File | Device |
| ---- | ------ |
| `pic.cpp` | 8259A interrupt controller, remapped so IRQ *n* arrives on vector 32+*n* |

A full PC platform directory — UEFI entry, ACPI tables, local APIC — is
milestone M7.
