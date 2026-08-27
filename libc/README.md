# ShirleyOS libc

The C library is shared between architectures. Only syscall trampolines are
allowed under `libc/arch/x86_64` and `libc/arch/arm64`.

The first user-facing slice provides `printf()` with `%s`, `%c`, `%d`, `%u`,
`%x`, and `%%`. It formats into a fixed 512-byte buffer and sends the result
through file descriptor 1; allocation is not required.
