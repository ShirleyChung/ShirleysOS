# Root file system

Everything under this directory is packed into the read-only SHRFS1 image the
kernel mounts at boot, so this file is also readable from the ShirleyOS shell:

    shirley:/$ cat /README.md

`cmake/make-rootfs.cmake` walks this tree at build time and emits a C++ byte
array, which is linked into the kernel and mounted through a RAM disk. Adding a
file here is all it takes for `ls` to show it after the next build; directories
are created from the paths themselves, so an empty directory is not preserved.

Two limits come from the image format: a single name is at most 55 bytes, and
the whole tree has to fit in the kernel image, which the x86_64 BIOS target
caps at 128 KiB in total.
