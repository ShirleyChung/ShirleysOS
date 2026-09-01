# hello

The same C source compiles for both ELF64 targets.

The image is packed into the kernel's file system as `/bin/hello` and started by
the shell's `hello` command (a shortcut for `exec /bin/hello`). It greets
through `printf`, then reads `/etc/version` with the `open`/`read`/`write`/
`close` syscalls to show a user program reaching the VFS itself, and returns
from `main()`. The `_start` stub turns that into the exit syscall, so control
returns to the shell, which prints the exit status and shows the prompt again —
the process teardown M3 filled in.
