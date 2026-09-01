# Process subsystem

`process.cpp` holds the running user process's file descriptor table. Before a
program is entered, `reset()` sets descriptors 0, 1, and 2 to standard input,
output, and error, all wired to the console; `open()` allocates descriptors from
3 up, each backed by a VFS descriptor. `read` and `write` route by descriptor to
the console or the VFS, `close` releases one, and `teardown()` closes whatever a
program left open when it exits. There is one process at a time, so the table is
a fixed-size static. This is the layer the `write`/`read`/`open`/`close`
syscalls call into; see `kernel/syscall/` and "User programs and system calls"
in `OS_SPEC.md`.

`scheduler.cpp` is a separate generic cooperative scheduler: a fixed-size task
table dispatched round-robin without heap allocation, each task running one
bounded quantum per callback and returning Ready, Blocked, or Finished. Timer
preemption and saved CPU contexts belong to the architecture layer.
