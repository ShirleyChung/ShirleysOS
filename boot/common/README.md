# Shared boot loader code

Code every ShirleyOS boot loader needs, regardless of which firmware it runs
under.

| File | Responsibility |
| ---- | -------------- |
| `elf64.cpp` | Read and validate an ELF64 kernel, and enumerate its `PT_LOAD` segments |
| `runtime.cpp` | The `memcpy`/`memset` family the compiler emits implicitly |

`elf64.cpp` never allocates and never touches hardware, so it compiles into the
host build and is covered by `tests/boot_loader_smoke.cpp`. A kernel image
comes from disk, so every declared offset and length is checked against the
buffer actually read: an ELF whose segment reaches past the end of the file is
rejected outright rather than loaded partially.

`runtime.cpp` exists separately from `kernel/runtime/` because a loader runs
inside the firmware environment and has no architecture layer to halt through.
