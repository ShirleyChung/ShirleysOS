# x86_64 architecture layer

ISA-specific code belongs here: page-table implementation, interrupt entry,
SYSCALL/Ring 3 transitions, and CPU initialization. It must not leak CR3,
PML4, or register conventions into `kernel/`.
