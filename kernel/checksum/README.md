# Checksums

Checksums that on-disk formats define. `crc32()` is the IEEE 802.3 CRC-32 that
zlib, Ethernet and GPT use; it takes the previous result so a large structure
can be checked in pieces as it is read, without holding all of it in memory.

The lookup table is computed at compile time and lands in `.rodata`, so there
is nothing to initialize at boot. ext4's CRC-32C (Castagnoli) will join it when
ext4 support arrives.
