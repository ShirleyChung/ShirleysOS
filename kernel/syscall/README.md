# Syscall subsystem

The dispatcher is shared; register save/restore and the entry instructions
remain in each architecture directory.

`syscall::dispatch()` decodes the shared `syscall::Context` an architecture
adapter fills (`rax`/`rdi`/`rsi`/`rdx` on x86_64, `x8`/`x0`/`x1`/`x2` on ARM64)
and routes each call: `write`/`read`/`open`/`close` go to `shirley::process`'s
file descriptor table, and `exit` tears the process's descriptors down and calls
`arch::exit_userspace()` to hand control back to the kernel code that launched
it. The numbers are defined in `include/shirley/syscall.hpp` and matched by the
libc trampolines and wrappers. See "User programs and system calls" in
`OS_SPEC.md`.
