# Drivers

Generic driver contracts live here; machine-specific implementations live in
their platform directory unless they are reusable device drivers.

`BlockDevice` is the sector-oriented disk contract. `RamDisk` supplies a
hardware-independent implementation for early kernel use and host tests; ATA,
NVMe, and virtio implementations remain platform drivers. The root file system
runs on a `RamDisk` today, which is what lets `kernel/fs/` be written against
the block interface before any real disk driver exists.

`input/` holds input logic that touches no hardware — today the PS/2 scancode
translation the PC keyboard driver uses. Splitting a driver along that line
puts its decision-making in the host build where tests can reach it, and
leaves only port access in the platform directory.

Every driver reaches interrupts through `shirley::irq`, never through an
interrupt controller directly.

A driver that produces or consumes bytes publishes itself as a
`shirley::device::Device` — see `kernel/device/README.md`. That is the whole of
what it owes its users: a name, a kind, and an operation table. An input driver
additionally hands its device to `shirley::console::attach_input()` once its
hardware really raises interrupts, which is what makes its characters reach the
shell. A driver never touches standard input, never echoes, and never knows the
shell exists.
