# SHRFS1

The image this file lives in is read-only and laid out as three regions:

    header   32 bytes   magic, version, entry count, table and data offsets
    table    80 bytes   one entry per file and directory
    data                every file's bytes, back to back

An entry carries a 56-byte name, a flag saying whether it is a directory, the
index of the directory it belongs to, and the offset and length of its data.
Entry 0 is the root, and it is its own parent, so walking up from anywhere
terminates without a special case.

Directories are not stored as lists of children. A directory listing is a scan
over the table for entries whose parent index matches, which costs nothing at
this size and removes the possibility of a listing disagreeing with the
entries it names.

The kernel reads the image through the ordinary block device interface, so the
same file system code will work over a real disk driver once one exists.
