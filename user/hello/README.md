# hello

The same C source is intended to compile for both ELF64 targets.

The image is linked into the kernel and started by the shell's `hello`
command. It does not return: there is no process teardown yet, so the program
takes over the CPU and the prompt does not come back until the machine
restarts. That is the missing piece M3 fills in.
