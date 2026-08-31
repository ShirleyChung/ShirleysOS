# ELF loader

ELF64 parsing and PT_LOAD mapping are shared. Only machine validation differs
for `EM_X86_64` and `EM_AARCH64`.

The image comes out of the file system. `kernel/user/launch.cpp` opens a path
through the VFS, reads the whole file into one buffer, and hands that buffer to
`load_elf()`, which allocates and maps a page per `PT_LOAD` page, copies the
file content in, zeroes the `.bss` tail, and allocates one writable stack page.
Nothing on the way down knows which file system or which device the bytes came
from — a path is the whole of what the loader is told.

The buffer is a fixed 64 KiB in `.bss`: the loader has to walk program headers
and therefore needs the image contiguous, and the kernel has no heap to size a
buffer from. A larger program is refused with a message rather than truncated
into an ELF that cannot be walked. The way past that limit is a loader that
reads each segment straight into its own pages, which waits for demand paging.
