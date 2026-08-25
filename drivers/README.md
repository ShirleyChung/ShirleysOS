# Drivers

Generic driver contracts live here; machine-specific implementations live in
their platform directory unless they are reusable device drivers.

`BlockDevice` is the sector-oriented disk contract. `RamDisk` supplies a
hardware-independent implementation for early kernel use and host tests;
ATA, NVMe, and virtio implementations remain platform drivers.
