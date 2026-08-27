# Drivers

Generic driver contracts live here; machine-specific implementations live in
their platform directory unless they are reusable device drivers.

`BlockDevice` is the sector-oriented disk contract. `RamDisk` supplies a
hardware-independent implementation for early kernel use and host tests; ATA,
NVMe, and virtio implementations remain platform drivers.

`input/` holds input logic that touches no hardware — today the PS/2 scancode
translation the PC keyboard driver uses. Splitting a driver along that line
puts its decision-making in the host build where tests can reach it, and
leaves only port access in the platform directory.

Every driver reaches interrupts through `shirley::irq`, never through an
interrupt controller directly.
