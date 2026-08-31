# ShirleyOS architecture boundaries

`arch/` represents CPU ISA and privilege mechanisms. `platform/` represents a
machine and its firmware/devices. Therefore Apple Silicon uses `arch/arm64`
plus `platform/apple_silicon`; `arch/apple_silicon` is forbidden.

Boot loaders normalize UEFI, device-tree, and future Apple firmware data into
the versioned `shirley::BootInfo` contract. Generic kernel code consumes only
that contract and the interfaces in `include/shirley/`.

Devices are reached through one chain, and each link knows only the next:

```text
Hardware → Driver → device::Device → registry → console
                                        ↓
                                      devfs → vfs → shell / ELF loader
```

`arch` ≠ `drivers` ≠ `device` ≠ `console` ≠ `fs`, and the inequalities run one
way. A keyboard driver may use x86 port I/O; the device manager does not know
what port 0x60 is; the console does not know about the 8259A; the shell does
not know about IRQ1; and the VFS knows nothing but the device abstraction.
`keyboard IRQ → shell` and `VFS → inb/outb` are the shapes this forbids.

`/dev` is a namespace over the registry, not a driver of its own: a devfs node
holds a `device::Device*` and forwards to its operation table, so `/dev/kbd0`
and `device::find("kbd0")` are the same object.

Above that, a path is the only name the rest of the kernel uses. The shell and
the ELF loader both reach files through `shirley::vfs` and neither knows which
file system, or which device, answered.

The first implementation slice deliberately keeps the generic startup and
physical-page accounting buildable while target-specific firmware and privilege
entry code is added one milestone at a time.
