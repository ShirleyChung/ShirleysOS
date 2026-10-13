# Generic memory management

Physical page accounting is architecture-independent. Page table operations
belong to the architecture implementation behind `AddressSpace`.

The early allocator normalizes usable firmware ranges, excludes page zero and
overlapping non-usable ranges, and uses fixed-size extent metadata so it does
not depend on heap allocation. `allocate_page()` returns zero on failure;
invalid and duplicate calls to `free_page()` are ignored.

`heap.cpp` is the kernel heap: an address-ordered free list with first fit,
splitting on allocation and merging neighbours on free. It manages whatever
region `heap::initialize()` is given; the kernel hands it a 1 MiB array in
`.bss`, because an ARM64 process address space maps only the kernel image and a
syscall must still be able to reach heap memory. Like `free_page()`, a free of
a foreign pointer or a double free is ignored. There is no `operator new`:
callers use `heap::allocate()` and `heap::free()` explicitly.
