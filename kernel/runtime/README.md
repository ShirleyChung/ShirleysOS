# Compiler runtime support

Routines the compiler emits calls to even under `-ffreestanding`: `memset`,
`memcpy`, `memmove`, and `memcmp` for structure copies and zeroing, plus
`operator delete` and `__cxa_pure_virtual` for polymorphic classes.

The kernel has no heap. Every `operator delete` halts the processor rather
than silently doing nothing, so an accidental `delete` becomes a visible stop
instead of quiet memory corruption.
