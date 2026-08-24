# ShirleyOS architecture boundaries

`arch/` represents CPU ISA and privilege mechanisms. `platform/` represents a
machine and its firmware/devices. Therefore Apple Silicon uses `arch/arm64`
plus `platform/apple_silicon`; `arch/apple_silicon` is forbidden.

Boot loaders normalize UEFI, device-tree, and future Apple firmware data into
the versioned `shirley::BootInfo` contract. Generic kernel code consumes only
that contract and the interfaces in `include/shirley/`.

The first implementation slice deliberately keeps the generic startup and
physical-page accounting buildable while target-specific firmware and privilege
entry code is added one milestone at a time.
