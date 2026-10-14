# Checksums

Checksums that on-disk formats define. `crc32()` is the IEEE 802.3 CRC-32 that
zlib, Ethernet and GPT use; it takes the previous result so a large structure
can be checked in pieces as it is read, without holding all of it in memory.

`crc32c()` is the Castagnoli CRC-32C of iSCSI and ext4's `metadata_csum`. ext4
stores it without the final inversion, so an ext4 checksum is `~crc32c(...)`.

The lookup tables are computed at compile time and land in `.rodata`, so there
is nothing to initialize at boot.
