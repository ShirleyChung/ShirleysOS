# File systems

Three things live here: the VFS, the SHRFS1 driver behind `/`, and devfs behind
`/dev`.

    shell / ELF loader
           ↓
          vfs           paths, the mount table, open files
        ↙     ↘
    shrfs     devfs
      ↓         ↓
  BlockDevice  device::Device

## The VFS

`vfs.cpp` is the only part that knows about paths. It normalizes one into an
absolute path, finds the mount with the longest matching mount point, and walks
the remainder inside that file system:

```cpp
const int descriptor = shirley::vfs::open("/bin/hello");
shirley::vfs::read(descriptor, buffer, sizeof(buffer));
shirley::vfs::close(descriptor);
```

Longest-match is what makes `/dev/kbd0` devfs's answer rather than the root file
system's, even though the path lies under both mounts. The match is by path
component, not by string prefix: `/devices` is not inside a mount at `/dev`.

A descriptor is an index into a fixed table holding a resolved node, a position,
and the access the open asked for. Asking for write on a read-only file system
fails at `open()` rather than at the first write, because by then a caller has
often already thrown away what it meant to save.

A resolved `vfs::Node` carries its own normalized absolute path. That is what
lets a listing include mount points: `ls /` shows `dev` even though the root
file system has no such directory, because `list()` appends the file systems
mounted directly inside a directory after that directory's own entries. A mount
point therefore needs no placeholder directory in the image — putting an empty
one into a read-only image purely to be covered up immediately would serve
nothing.

Block devices are reachable two ways through one descriptor. `read()` and
`write()` address bytes and let devfs translate the file position into blocks,
reading a block whole and taking the wanted slice out of it. `block_read()` and
`block_write()` address sectors directly and ignore the position — that is the
path a file system driver takes, and it is what `blk /dev/ram0 0` uses to show
the SHRFS1 header the mount itself checked.

## SHRFS1

`shrfs.cpp` mounts one read-only SHRFS1 volume through the block device
interface, and `shrfs_vfs.cpp` presents it to the VFS. `mount_rootfs()` wraps
the image `cmake/make-rootfs.cmake` generates from `rootfs/` in a
`shirley::io::RamDisk`, so the kernel has files to read before any disk driver
exists, and publishes that disk in the device registry as `ram0`. Every access
goes through `io::BlockDevice`, which is what will let the same code work over a
real disk unchanged — `shrfs.cpp` never touches the image's memory directly and
reads through one block-sized buffer.

### The layout

    header   32 bytes   magic, version, entry count, table and data offsets
    table    80 bytes   per entry: name[56], flags, parent, data offset, size
    data                file contents, back to back

A directory stores no list of children. Each entry records the index of the
directory holding it, and a listing is a scan for entries whose parent matches,
which is why a listing can never disagree with the entries it names. Entry 0 is
the root and is its own parent, so walking up from anywhere terminates without
a special case.

Everything is decoded byte by byte as little-endian, so the kernel's struct
padding cannot change how an image is read. `mount()` validates the header and
then decodes every entry once: a corrupt image fails at mount time rather than
surprising the first `ls`.

The format is read-only. A writable one needs block allocation and an updated
entry table; until then `write()` reports Unsupported rather than pretending.

`/bin/hello` is in the image too. The build packs the user program it just
linked in alongside `rootfs/`, which is what lets the kernel read a program out
of its own file system and hand it to the ELF loader instead of running bytes
linked into itself.

## devfs

`devfs.cpp` has no storage and holds no device state. Its content is whatever
the device registry holds right now, so `/dev/kbd0` and `device::find("kbd0")`
are the same object and the two can never disagree. That is what it means to say
`/dev` is a namespace rather than a driver.

It is one level deep — devices have no hierarchy and inventing one would only be
another thing to maintain. Whether a node can be written is asked of the device
by writing zero bytes to it: kbd0 uses the generic `ByteStream` adapter table,
which has a write, while the input queue behind it refuses every one. A write of
zero bytes moves nothing by the `io::Result` contract, so asking costs nothing
and asks exactly the right question.
