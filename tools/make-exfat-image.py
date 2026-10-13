#!/usr/bin/env python3
# 產生 exFAT 測試映像 tests/images/exfat-basic.img。
#
# 開發機是 Windows，沒有 mkfs.exfat，一般使用者也不能把 VHD 格式化，因此這個
# 映像由這支程式直接依照 Microsoft 的 exFAT 規格寫出來。它刻意不共用核心的
# 任何程式碼：讀取端與寫入端各自依規格實作，兩邊理解不一致時測試才會失敗。
# 讀取端另外也拿 exfatprogs 的 mkfs.exfat 產生的映像驗證過。
#
# 內容涵蓋讀取端必須處理的每一種情況：連續配置（NoFatChain）與 FAT 串接、
# 不照順序串接的叢集、跨越叢集邊界的項目組、ValidDataLength 小於 DataLength、
# 空檔案、長的非 ASCII 名稱、已刪除、校驗和錯誤、雜湊錯誤與名稱不合法的項目組、
# 壓縮的大寫表。
#
# --clean 產生 tests/images/exfat-clean.img：同樣的內容，但不含任何損壞的項目組，
# 給寫入測試當起點，寫完之後交給 tools/check-exfat-image.py 檢查。
#
# 用法：python tools/make-exfat-image.py [--clean] [輸出路徑]
#
# Generate the exFAT test image tests/images/exfat-basic.img.
#
# The development machine is Windows: there is no mkfs.exfat, and an ordinary
# user cannot format a VHD either, so this program writes the image directly
# from Microsoft's exFAT specification. It deliberately shares no code with the
# kernel: the reader and this writer each follow the specification on their
# own, and a test fails only when the two understand it differently. The
# reader has also been checked against images made by exfatprogs' mkfs.exfat.
#
# The content covers every case the reader has to handle: contiguous
# (NoFatChain) and FAT-chained allocation, clusters chained out of order, an
# entry set straddling a cluster boundary, ValidDataLength below DataLength, an
# empty file, a long non-ASCII name, deleted, bad-checksum, bad-hash and
# illegally named entry sets, and a compressed up-case table.
#
# --clean writes tests/images/exfat-clean.img instead: the same content without
# any damaged entry set, the starting point for the write tests, whose result
# tools/check-exfat-image.py then checks.
#
# Usage: python tools/make-exfat-image.py [--clean] [output path]

import os
import struct
import sys

SECTOR_SHIFT = 9
CLUSTER_SECTOR_SHIFT = 3
SECTOR = 1 << SECTOR_SHIFT
CLUSTER = SECTOR << CLUSTER_SECTOR_SHIFT
VOLUME_SECTORS = 2048  # 1 MiB
FAT_OFFSET = 128
FAT_LENGTH = 128
HEAP_OFFSET = 256
CLUSTER_COUNT = (VOLUME_SECTORS - HEAP_OFFSET) >> CLUSTER_SECTOR_SHIFT
SERIAL = 0x5348524C
# 2026-10-13 12:00:00，DOS 時間戳記格式。 / In DOS timestamp format.
TIMESTAMP = ((2026 - 1980) << 25) | (10 << 21) | (13 << 16) | (12 << 11)

CLEAN = "--clean" in sys.argv[1:]
ARGUMENTS = [argument for argument in sys.argv[1:] if argument != "--clean"]

image = bytearray(VOLUME_SECTORS * SECTOR)
fat = [0] * (CLUSTER_COUNT + 2)
fat[0] = 0xFFFFFFF8
fat[1] = 0xFFFFFFFF
allocated = set()
next_free = 2


def checksum32(data, skip=()):
    total = 0
    for index, byte in enumerate(data):
        if index in skip:
            continue
        total = (((total & 1) << 31) | (total >> 1)) + byte
        total &= 0xFFFFFFFF
    return total


def checksum16(data, skip=()):
    total = 0
    for index, byte in enumerate(data):
        if index in skip:
            continue
        total = (((total & 1) << 15) | (total >> 1)) + byte
        total &= 0xFFFF
    return total


def cluster_offset(cluster):
    return HEAP_OFFSET * SECTOR + (cluster - 2) * CLUSTER


def skip(count):
    """跳過 count 個叢集，留著不配置，讓之後的配置不連續。
    Skip count clusters, leaving them unallocated, so later allocations are not
    contiguous."""
    global next_free
    next_free += count


def take(count):
    """配置 count 個連續叢集。 / Allocate count consecutive clusters."""
    global next_free
    first = next_free
    next_free += count
    assert next_free <= CLUSTER_COUNT + 2, "volume full"
    allocated.update(range(first, first + count))
    return first


def place(data, clusters, contiguous):
    """把資料寫進給定的叢集；需要時串成 FAT 鏈。
    Write data into the given clusters, chaining them in the FAT when asked."""
    for index, cluster in enumerate(clusters):
        chunk = data[index * CLUSTER:(index + 1) * CLUSTER]
        start = cluster_offset(cluster)
        image[start:start + len(chunk)] = chunk
        if not contiguous:
            fat[cluster] = clusters[index + 1] if index + 1 < len(clusters) else 0xFFFFFFFF


def build_upcase():
    """以 Python 的 str.upper() 建出大寫表，連續的「對應到自己」壓縮成 0xFFFF, n。
    Build the up-case table from str.upper(), compressing identity runs into
    0xFFFF, n."""
    mapping = []
    for unit in range(0x10000):
        upper = unit
        if not 0xD800 <= unit <= 0xDFFF:
            converted = chr(unit).upper()
            if len(converted) == 1 and ord(converted) <= 0xFFFF:
                upper = ord(converted)
        mapping.append(upper)
    while mapping and mapping[-1] == len(mapping) - 1:
        mapping.pop()
    table = []
    index = 0
    while index < len(mapping):
        run = 0
        while index + run < len(mapping) and mapping[index + run] == index + run:
            run += 1
        if run >= 3:
            table += [0xFFFF, run]
            index += run
        else:
            table.append(mapping[index])
            index += 1
    return mapping, struct.pack("<%dH" % len(table), *table)


UPCASE_MAP, UPCASE_BYTES = build_upcase()


def upcase(unit):
    return UPCASE_MAP[unit] if unit < len(UPCASE_MAP) else unit


def name_units(name):
    encoded = name.encode("utf-16-le")
    return list(struct.unpack("<%dH" % (len(encoded) // 2), encoded))


def name_hash(units):
    data = bytearray()
    for unit in units:
        data += struct.pack("<H", upcase(unit))
    return checksum16(data)


def entry_set(name, attributes, first_cluster, length, valid_length=None, contiguous=True):
    """一組 File + Stream Extension + File Name 項目。
    One File + Stream Extension + File Name entry set."""
    if valid_length is None:
        valid_length = length
    units = name_units(name)
    assert 1 <= len(units) <= 255
    name_entries = (len(units) + 14) // 15
    secondary = 1 + name_entries
    entries = bytearray(32 * (1 + secondary))
    struct.pack_into("<BBHH", entries, 0, 0x85, secondary, 0, attributes)
    struct.pack_into("<III", entries, 8, TIMESTAMP, TIMESTAMP, TIMESTAMP)
    flags = 0x01 | (0x02 if contiguous else 0)
    struct.pack_into("<BBBBH", entries, 32, 0xC0, flags, 0, len(units), name_hash(units))
    struct.pack_into("<Q", entries, 40, valid_length)
    struct.pack_into("<IQ", entries, 52, first_cluster, length)
    for index in range(name_entries):
        base = 64 + index * 32
        entries[base] = 0xC1
        chunk = units[index * 15:(index + 1) * 15]
        struct.pack_into("<%dH" % len(chunk), entries, base + 2, *chunk)
    struct.pack_into("<H", entries, 2, checksum16(entries, skip=(2, 3)))
    return entries


def add_file(directory, name, data, contiguous=True, valid_length=None, clusters=None,
             slack=b""):
    """配置叢集、寫入內容，並把項目組加進目錄。slack 是寫在 ValidDataLength
    之後的垃圾，讀取端必須把那一段讀成零。

    Allocate clusters, write the content, and add the entry set to the
    directory. slack is garbage written past ValidDataLength, which the reader
    must read back as zeros."""
    stored = data + slack
    length = len(stored)
    if length == 0:
        directory.append(entry_set(name, 0x20, 0, 0, 0))
        return
    count = (length + CLUSTER - 1) // CLUSTER
    if clusters is None:
        first = take(count)
        clusters = list(range(first, first + count))
    place(stored, clusters, contiguous)
    directory.append(entry_set(name, 0x20, clusters[0], length,
                               len(data) if valid_length is None else valid_length,
                               contiguous))


def add_directory(parent, name, entries, cluster_list, contiguous):
    content = b"".join(entries)
    assert len(content) <= len(cluster_list) * CLUSTER
    place(content, cluster_list, contiguous)
    parent.append(entry_set(name, 0x10, cluster_list[0], len(cluster_list) * CLUSTER,
                            contiguous=contiguous))


def pattern(length, step):
    return bytes((index * step + 1) & 0xFF for index in range(length))


# 系統結構：配置位元圖、大寫表、根目錄（兩個不相鄰的叢集串成 FAT 鏈）。
# System structures: allocation bitmap, up-case table, and the root directory
# (two non-adjacent clusters chained through the FAT).
bitmap_cluster = take(1)
upcase_clusters = (len(UPCASE_BYTES) + CLUSTER - 1) // CLUSTER
upcase_first = take(upcase_clusters)
root_first = take(1)
skip(1)
root_second = take(1)
root_clusters = [root_first, root_second]

root = []
label = name_units("SHIRLEY")
label_entry = bytearray(32)
label_entry[0] = 0x83
label_entry[1] = len(label)
struct.pack_into("<%dH" % len(label), label_entry, 2, *label)
root.append(label_entry)
bitmap_entry = bytearray(32)
root.append(bitmap_entry)  # 長度最後才知道 / length filled in at the end
upcase_entry = bytearray(32)
upcase_entry[0] = 0x82
struct.pack_into("<I", upcase_entry, 4, checksum32(UPCASE_BYTES))
struct.pack_into("<IQ", upcase_entry, 20, upcase_first, len(UPCASE_BYTES))
root.append(upcase_entry)
place(UPCASE_BYTES, list(range(upcase_first, upcase_first + upcase_clusters)), False)

add_file(root, "README.TXT", b"Hello from exFAT!\n")

# 已刪除的項目組：只清掉每個項目的使用中位元。
# A deleted entry set: only each entry's in-use bit is cleared.
ghost = entry_set("GHOST.TXT", 0x20, 0, 0)
for index in range(0, len(ghost), 32):
    ghost[index] &= 0x7F
root.append(ghost)

add_file(root, "empty.txt", b"")

# 不照順序串接的叢集，中間夾著沒有配置的空隙。
# Clusters chained out of order, with unallocated gaps in between.
base = next_free
skip(7)
fragments = [base + 4, base, base + 6, base + 2]
allocated.update(fragments)
add_file(root, "fragmented.bin", pattern(14000, 7), contiguous=False, clusters=fragments)

add_file(root, "prealloc.bin", pattern(5000, 3), slack=b"\xEE" * 3192)
add_file(root, "big.bin", pattern(20000, 13))

# 校驗和錯誤的項目組，讀取端必須跳過它。
# An entry set with a bad checksum, which the reader must skip.
if not CLEAN:
    broken = entry_set("BROKEN.TXT", 0x20, 0, 0)
    broken[2] ^= 0xFF
    root.append(broken)

# 名稱雜湊錯誤（校驗和重算過，所以只有雜湊不對）、含非法字元的名稱、名為 ".."
# 的項目：讀取端都必須跳過，否則會列出查不到的名字，或讓走訪繞回自己。
# A wrong name hash (with the checksum recomputed, so only the hash is off), a
# name with an illegal character, and an entry called "..": the reader must
# skip all three, or it lists names it cannot look up, or a walk loops.
if not CLEAN:
    bad_hash = entry_set("BADHASH.TXT", 0x20, 0, 0)
    bad_hash[36] ^= 0x01
    struct.pack_into("<H", bad_hash, 2, checksum16(bad_hash, skip=(2, 3)))
    root.append(bad_hash)
    root.append(entry_set("a:b.txt", 0x20, 0, 0))
    root.append(entry_set("..", 0x10, 0, 0))

# docs/ 與 docs/sub/：長的中文名稱、大小寫混合的名稱、巢狀目錄。
# docs/ and docs/sub/: a long Chinese name, a mixed-case name, nesting.
long_name = "這是一個很長的中文檔名_with_ASCII_and_more_than_fifteen_units.txt"
sub = []
add_file(sub, "deep.txt", b"deep inside\n")
docs = []
add_file(docs, long_name, "長檔名內容\n".encode("utf-8"))
add_file(docs, "Mixed Case.Md", b"# mixed\n")
add_directory(docs, "sub", sub, [take(1)], True)
add_directory(root, "docs", docs, [take(1)], True)

# many/：60 個檔案、180 個項目，跨兩個不相鄰的叢集；第 43 組項目跨越叢集邊界。
# many/: 60 files and 180 entries over two non-adjacent clusters; the 43rd
# entry set straddles the cluster boundary.
many = []
for number in range(60):
    add_file(many, "f%03d.txt" % number, ("file %03d\n" % number).encode())
first = take(1)
skip(1)
second = take(1)
add_directory(root, "many", many, [first, second], False)

place(b"".join(root), root_clusters, False)

# 配置位元圖最後填：所有叢集都已經配置完了。
# The allocation bitmap is filled last, once every cluster has been allocated.
bitmap = bytearray((CLUSTER_COUNT + 7) // 8)
for cluster in allocated:
    bitmap[(cluster - 2) // 8] |= 1 << ((cluster - 2) % 8)
place(bytes(bitmap), [bitmap_cluster], False)
bitmap_entry[0] = 0x81
struct.pack_into("<IQ", bitmap_entry, 20, bitmap_cluster, len(bitmap))
place(b"".join(root), root_clusters, False)

for index, value in enumerate(fat):
    struct.pack_into("<I", image, FAT_OFFSET * SECTOR + index * 4, value)

# 開機區：主開機磁區、8 個延伸開機磁區、OEM 與保留磁區、校驗和磁區；接著是
# 一份完全相同的備份。
# The boot region: main boot sector, 8 extended boot sectors, OEM and reserved
# sectors, the checksum sector; then an identical backup.
boot = bytearray(12 * SECTOR)
boot[0:3] = b"\xEB\x76\x90"
boot[3:11] = b"EXFAT   "
struct.pack_into("<QQIIIIIIH", boot, 64, 0, VOLUME_SECTORS, FAT_OFFSET, FAT_LENGTH,
                 HEAP_OFFSET, CLUSTER_COUNT, root_first, SERIAL, 0x0100)
struct.pack_into("<HBBBBB", boot, 106, 0, SECTOR_SHIFT, CLUSTER_SECTOR_SHIFT, 1, 0x80,
                 len(allocated) * 100 // CLUSTER_COUNT)
boot[510:512] = b"\x55\xAA"
for sector in range(1, 9):
    boot[sector * SECTOR + SECTOR - 4:sector * SECTOR + SECTOR] = b"\x00\x00\x55\xAA"
boot_sum = checksum32(boot[:11 * SECTOR], skip=(106, 107, 112))
for offset in range(11 * SECTOR, 12 * SECTOR, 4):
    struct.pack_into("<I", boot, offset, boot_sum)
image[0:len(boot)] = boot
image[len(boot):2 * len(boot)] = boot

output = ARGUMENTS[0] if ARGUMENTS else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "tests", "images",
    "exfat-clean.img" if CLEAN else "exfat-basic.img")
os.makedirs(os.path.dirname(os.path.abspath(output)), exist_ok=True)
with open(output, "wb") as handle:
    handle.write(image)
print("wrote %s: %d clusters of %d used" % (output, len(allocated), CLUSTER_COUNT))
