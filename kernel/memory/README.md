# Generic memory management

Physical page accounting is architecture-independent. Page table operations
belong to the architecture implementation behind `AddressSpace`.

The early allocator normalizes usable firmware ranges, excludes page zero and
overlapping non-usable ranges, and uses fixed-size extent metadata so it does
not depend on heap allocation. `allocate_page()` returns zero on failure;
invalid and duplicate calls to `free_page()` are ignored.
