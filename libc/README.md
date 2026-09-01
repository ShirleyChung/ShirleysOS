# ShirleyOS libc

The C library is shared between architectures. Only syscall trampolines are
allowed under `libc/arch/x86_64` and `libc/arch/arm64`.

The first user-facing slice provides `printf()` with `%s`, `%c`, `%d`, `%u`,
`%x`, and `%%`. It formats into a fixed 512-byte buffer and sends the result
through file descriptor 1; allocation is not required.

Alongside it are the thin POSIX-style wrappers over the native syscalls:
`write` and `read` (`unistd.h`), `open` (`fcntl.h`, with `O_RDONLY`/`O_WRONLY`/
`O_RDWR`), `close`, and `_exit`. Each is one call into the architecture
trampoline `shirley_syscall` with the number from
`include/shirley/syscall.hpp`. A program returning from `main()` runs the exit
syscall through the `_start` stub, so control returns to the shell that launched
it.
