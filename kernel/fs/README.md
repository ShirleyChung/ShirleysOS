# File system

`shirley::fs` mounts one read-only SHRFS1 volume through the block device
interface:

```cpp
shirley::fs::mount_rootfs();          // the image built into the kernel
shirley::fs::Node node;
shirley::fs::lookup("/etc/motd", node);
shirley::fs::read(node, 0, buffer, sizeof(buffer));
```

`mount_rootfs()` wraps the image `cmake/make-rootfs.cmake` generates from
`rootfs/` in a `shirley::io::RamDisk`, so the kernel has files to read before
any disk driver exists. Every access goes through `io::BlockDevice`, which is
what will let the same code work over a real disk unchanged — `shrfs.cpp` never
touches the image's memory directly and reads through one block-sized buffer.

## The layout

    header   32 bytes   magic, version, entry count, table and data offsets
    table    80 bytes   per entry: name[56], flags, parent, data offset, size
    data                file contents, back to back

A directory stores no list of children. Each entry records the index of the
directory holding it, and a listing is a scan for entries whose parent matches,
which is why a listing can never disagree with the entries it names. Entry 0 is
the root and is its own parent, so walking up from anywhere terminates without
a special case — `path_of()` relies on exactly that.

Everything is decoded byte by byte as little-endian, so the kernel's struct
padding cannot change how an image is read. `mount()` validates the header and
then decodes every entry once: a corrupt image fails at mount time rather than
surprising the first `ls`.

## Paths

`lookup()` walks components rather than rewriting the string, handling `.` and
`..` as it goes and skipping repeated slashes. A path starting with `/` is
resolved from the root and any other from the `base` node the caller passes,
which is how the shell resolves relative paths against its working directory.
