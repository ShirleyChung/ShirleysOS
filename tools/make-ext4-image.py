#!/usr/bin/env python3
# 產生 ext4 與 ext2 的測試映像：tests/images/ext4-basic.img、ext4-metabg.img 與
# ext2-basic.img。
#
# 開發機是 Windows，沒有 mke2fs，因此映像由這支程式依照 Linux 核心文件的 ext4
# 磁碟格式說明直接寫出來。它刻意不共用核心的任何程式碼：讀取端與寫入端各自
# 依規格實作，兩邊理解不一致時測試才會失敗。產生的映像另外用 7-Zip 解開並逐
# 位元組比對過，確認第三方的實作看到的也是同樣的內容；7-Zip 不支援的兩處——
# meta_bg 與三層間接區塊——只有核心的讀取端驗證。
#
# ext4-basic.img：1 KiB 區塊、32 個區塊群組、64bit、extents、filetype、
# dir_index、metadata_csum（所有校驗和都算好）。內容涵蓋：深度 1 的 extent 樹、
# 洞、未初始化的 extent（磁碟上故意放了非零資料）、遠大於卷冊的稀疏檔、空檔、
# 非 ASCII 與 255 位元組的名稱、快速與一般的符號連結、裝置檔、跨好幾個區塊的
# 目錄、一個 htree 目錄、藏在前一個項目 rec_len 裡的已刪除項目、硬連結，以及
# 分散在第一與最後一個群組的 inode。
#
# ext4-metabg.img（--meta-bg）：同樣的內容，但開啟 meta_bg：第 16 到 31 個群組的
# 描述子放在第 16 個群組的開頭，而不是緊接在超級區塊之後。
#
# ext2-basic.img（--ext2）：2 KiB 區塊、修訂版 1 但沒有任何 incompat 功能（目錄
# 項目的名稱長度是 16 位元）、128 位元組的 inode、舊式區塊對應：直接、一、二、
# 三層間接區塊都用到。
#
# 用法：python tools/make-ext4-image.py [--ext2 | --meta-bg] [輸出路徑]
#
# Generate the ext4 and ext2 test images: tests/images/ext4-basic.img,
# ext4-metabg.img and ext2-basic.img.
#
# The development machine is Windows and has no mke2fs, so this program writes
# the images directly from the ext4 disk layout description in the Linux kernel
# documentation. It deliberately shares no code with the kernel: the reader
# and this writer each follow the description on their own, and a test fails
# only when the two understand it differently. The images have also been
# unpacked with 7-Zip and compared byte for byte, to confirm that a third-party
# implementation sees the same content; the two things 7-Zip does not support,
# meta_bg and triple indirect blocks, are checked by the kernel's reader alone.
#
# ext4-basic.img: 1 KiB blocks, 32 block groups, 64bit, extents,
# filetype, dir_index and metadata_csum (every checksum filled in). The content
# covers a depth-1 extent tree, holes, an uninitialized extent (with non-zero
# bytes deliberately left on disk), a sparse file far larger than the volume,
# an empty file, non-ASCII and 255-byte names, fast and ordinary symbolic
# links, a device file, a directory spanning several blocks, an htree
# directory, a deleted entry hidden inside the previous entry's rec_len, hard
# links, and inodes spread over the first and last groups.
#
# ext4-metabg.img (--meta-bg): the same content with meta_bg on: the
# descriptors of groups 16 to 31 sit at the start of group 16 instead of right
# after the superblock.
#
# ext2-basic.img (--ext2): 2 KiB blocks, revision 1 but no incompat feature at
# all (directory entries have 16-bit name lengths), 128-byte inodes, and the
# old block map with direct, single, double and triple indirect blocks in use.
#
# Usage: python tools/make-ext4-image.py [--ext2 | --meta-bg] [output path]

import os
import struct
import sys

COMPAT_EXT_ATTR = 0x0008
COMPAT_DIR_INDEX = 0x0020
INCOMPAT_FILETYPE = 0x0002
INCOMPAT_META_BG = 0x0010
INCOMPAT_EXTENTS = 0x0040
INCOMPAT_64BIT = 0x0080
RO_SPARSE_SUPER = 0x0001
RO_LARGE_FILE = 0x0002
RO_HUGE_FILE = 0x0008
RO_DIR_NLINK = 0x0020
RO_EXTRA_ISIZE = 0x0040
RO_METADATA_CSUM = 0x0400

INODE_FLAG_INDEX = 0x1000
INODE_FLAG_EXTENTS = 0x80000

S_IFDIR = 0x4000
S_IFREG = 0x8000
S_IFLNK = 0xA000
S_IFCHR = 0x2000

FIRST_INODE = 11
TIMESTAMP = 1791892800  # 2026-10-13 12:00:00 UTC


def make_crc32c_table():
    table = []
    for index in range(256):
        value = index
        for _ in range(8):
            value = (value >> 1) ^ 0x82F63B78 if value & 1 else value >> 1
        table.append(value)
    return table


CRC32C_TABLE = make_crc32c_table()


# ext4 的 CRC-32C 沒有最後的反相：傳入上一段的原始值，回傳原始值。
# ext4's CRC-32C has no final inversion: it takes the previous raw value and
# returns the raw value.
def crc32c(crc, data):
    for byte in data:
        crc = CRC32C_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8)
    return crc


def pattern(length, step):
    return bytes(((i * step + 1) & 0xFF) for i in range(length))


def power_of(value, base):
    power = base
    while power < value:
        power *= base
    return power == value


class Node:
    def __init__(self, number, kind, parent):
        self.number = number
        self.kind = kind
        self.parent = parent
        self.children = []  # (name, node, ghost)
        self.indexed = False
        self.links = 1 if kind != S_IFDIR else 2
        self.mode = kind | (0o755 if kind == S_IFDIR else 0o644)
        self.size = 0
        self.flags = 0
        self.block = bytes(60)
        self.blocks512 = 0
        self.group = 0
        self.dir_blocks = 1


class Volume:
    def __init__(self, block, blocks, per_group, inodes_per_group, inode_size, compat,
                 incompat, ro_compat, uuid, label):
        self.block = block
        self.blocks = blocks
        self.per_group = per_group
        self.inodes_per_group = inodes_per_group
        self.inode_size = inode_size
        self.compat = compat
        self.incompat = incompat
        self.ro_compat = ro_compat
        self.uuid = uuid
        self.label = label
        self.first_data_block = 1 if block == 1024 else 0
        self.groups = (blocks - self.first_data_block + per_group - 1) // per_group
        self.desc_size = 64 if incompat & INCOMPAT_64BIT else 32
        self.extents = bool(incompat & INCOMPAT_EXTENTS)
        self.filetype = bool(incompat & INCOMPAT_FILETYPE)
        self.csum = bool(ro_compat & RO_METADATA_CSUM)
        self.seed = crc32c(0xFFFFFFFF, uuid) if self.csum else 0
        self.image = bytearray(blocks * block)
        self.used_blocks = set()
        self.used_inodes = set(range(1, FIRST_INODE))
        self.nodes = {}
        self.layout()
        self.root = Node(2, S_IFDIR, None)
        self.root.parent = self.root
        self.nodes[2] = self.root

    # 區塊群組的配置：備份超級區塊、描述子、兩個位元圖、inode 表。
    # Block group layout: backup superblock, descriptors, two bitmaps, inode
    # table.
    def first_block(self, group):
        return self.first_data_block + group * self.per_group

    def has_super(self, group):
        if group <= 1:
            return True
        if group % 2 == 0:
            return False
        return power_of(group, 3) or power_of(group, 5) or power_of(group, 7)

    def descriptor_blocks_in(self, group):
        per_block = self.block // self.desc_size
        if self.incompat & INCOMPAT_META_BG:
            # first_meta_bg 為 0：每個 meta 群組的第一、第二與最後一個群組各放
            # 一份。 / first_meta_bg is 0: the first, second and last group of
            # each meta group hold a copy.
            position = group % per_block
            meta = group // per_block
            last = min(per_block, self.groups - meta * per_block) - 1
            if position in (0, 1, last):
                return [(meta, 1)]
            return []
        if self.has_super(group):
            return [(0, (self.groups * self.desc_size + self.block - 1) // self.block)]
        return []

    def layout(self):
        self.inode_table_blocks = self.inodes_per_group * self.inode_size // self.block
        self.group_meta = []
        if self.first_data_block:
            self.used_blocks.add(0)
        for group in range(self.groups):
            at = self.first_block(group)
            descriptors = []
            if self.has_super(group):
                self.used_blocks.add(at)
                at += 1
            for meta, count in self.descriptor_blocks_in(group):
                descriptors.append((at, meta, count))
                for i in range(count):
                    self.used_blocks.add(at + i)
                at += count
            block_bitmap, inode_bitmap, inode_table = at, at + 1, at + 2
            for i in range(2 + self.inode_table_blocks):
                self.used_blocks.add(at + i)
            self.group_meta.append({'descriptors': descriptors, 'block_bitmap': block_bitmap,
                                    'inode_bitmap': inode_bitmap, 'inode_table': inode_table})

    def allocate(self, count, group):
        at = self.first_block(group)
        end = min(self.blocks, self.first_block(group) + self.per_group)
        while at + count <= end:
            if all(at + i not in self.used_blocks for i in range(count)):
                for i in range(count):
                    self.used_blocks.add(at + i)
                return at
            at += 1
        if group + 1 < self.groups:
            return self.allocate(count, group + 1)
        raise RuntimeError('volume full')

    def new_inode(self, kind, parent, group):
        for number in range(group * self.inodes_per_group + 1, (group + 1) * self.inodes_per_group + 1):
            if number not in self.used_inodes:
                self.used_inodes.add(number)
                node = Node(number, kind, parent)
                node.group = group
                self.nodes[number] = node
                return node
        raise RuntimeError('group out of inodes')

    def link(self, parent, name, node, ghost=False):
        parent.children.append((name, node, ghost))

    def mkdir(self, parent, name, group, blocks=1):
        node = self.new_inode(S_IFDIR, parent, group)
        node.dir_blocks = blocks
        self.link(parent, name, node)
        parent.links += 1
        return node

    def inode_seed(self, node):
        return crc32c(crc32c(self.seed, struct.pack('<I', node.number)), struct.pack('<I', 0))

    # 把內容放上磁碟。pieces 是 (邏輯區塊, 位元組, 未初始化) 的串列；沒有被涵蓋的
    # 區塊是洞。 / Put content on disk. pieces is a list of (logical block,
    # bytes, uninitialized); blocks not covered are holes.
    def store(self, node, pieces, group):
        runs = []
        for logical, data, uninitialized in pieces:
            count = (len(data) + self.block - 1) // self.block
            physical = self.allocate(count, group)
            if uninitialized:
                # 未初始化的 extent 讀起來必須是零；在磁碟上放垃圾，讀取端如果
                # 照實讀出來就會被抓到。 / An uninitialized extent must read as
                # zero; leave junk on disk so a reader that returns it is caught.
                data = b'\xEE' * (count * self.block)
            start = physical * self.block
            self.image[start:start + len(data)] = data
            runs.append((logical, count, physical, uninitialized))
            node.blocks512 += count * self.block // 512
        if self.extents:
            self.build_extents(node, runs, group)
        else:
            self.build_indirect(node, runs, group)

    def extent_header(self, entries, capacity, depth):
        return struct.pack('<HHHHI', 0xF30A, entries, capacity, depth, 0)

    def build_extents(self, node, runs, group):
        node.flags |= INODE_FLAG_EXTENTS
        leaves = []
        for logical, count, physical, uninitialized in sorted(runs):
            length = count + 32768 if uninitialized else count
            leaves.append(struct.pack('<IHHI', logical, length, physical >> 32, physical & 0xFFFFFFFF))
        if len(leaves) <= 4:
            node.block = (self.extent_header(len(leaves), 4, 0) + b''.join(leaves)).ljust(60, b'\0')
            return
        # 深度 1：一個葉節點區塊，根裡只有一個索引項目。
        # Depth 1: one leaf block, with a single index entry in the root.
        capacity = (self.block - 12) // 12
        assert len(leaves) <= capacity
        leaf = self.allocate(1, group)
        content = bytearray(self.block)
        body = self.extent_header(len(leaves), capacity, 0) + b''.join(leaves)
        content[:len(body)] = body
        if self.csum:
            tail = 12 + capacity * 12
            struct.pack_into('<I', content, tail, crc32c(self.inode_seed(node), content[:tail]))
        self.image[leaf * self.block:(leaf + 1) * self.block] = content
        node.blocks512 += self.block // 512
        first = sorted(runs)[0][0]
        index = struct.pack('<IIHH', first, leaf & 0xFFFFFFFF, leaf >> 32, 0)
        node.block = (self.extent_header(1, 4, 1) + index).ljust(60, b'\0')

    def build_indirect(self, node, runs, group):
        per = self.block // 4
        mapping = {}
        for logical, count, physical, _ in runs:
            for i in range(count):
                mapping[logical + i] = physical + i
        pointers = [0] * 15
        tables = {}

        def table(path):
            if path not in tables:
                tables[path] = self.allocate(1, group)
                node.blocks512 += self.block // 512
            return tables[path]

        def put(block, index, value):
            struct.pack_into('<I', self.image, block * self.block + index * 4, value)

        for logical, physical in sorted(mapping.items()):
            if logical < 12:
                pointers[logical] = physical
                continue
            rest = logical - 12
            levels, span = 1, per
            while rest >= span:
                rest -= span
                levels += 1
                span *= per
            path = (levels,)
            pointers[11 + levels] = table(path)
            current = table(path)
            for _ in range(levels - 1):
                span //= per
                index = rest // span
                rest %= span
                child_path = path + (index,)
                put(current, index, table(child_path))
                path = child_path
                current = table(path)
            put(current, rest, physical)
        node.block = struct.pack('<15I', *pointers)

    def file(self, parent, name, content=b'', group=0, pieces=None, size=None, kind=S_IFREG):
        node = self.new_inode(kind, parent, group)
        self.link(parent, name, node)
        if pieces is None:
            pieces = [(0, content, False)] if content else []
        node.size = size if size is not None else len(content)
        if pieces:
            self.store(node, pieces, group)
        elif self.extents and kind == S_IFREG:
            node.flags |= INODE_FLAG_EXTENTS
            node.block = self.extent_header(0, 4, 0).ljust(60, b'\0')
        return node

    def symlink(self, parent, name, target, group=0):
        if len(target) < 60:
            node = self.new_inode(S_IFLNK, parent, group)
            self.link(parent, name, node)
            node.mode = S_IFLNK | 0o777
            node.size = len(target)
            node.block = target.ljust(60, b'\0')
            return node
        node = self.file(parent, name, target, group)
        node.kind = S_IFLNK
        node.mode = S_IFLNK | 0o777
        return node

    def device(self, parent, name, major, minor, group=0):
        node = self.new_inode(S_IFCHR, parent, group)
        self.link(parent, name, node)
        node.mode = S_IFCHR | 0o666
        node.block = struct.pack('<I', (major << 8) | minor).ljust(60, b'\0')
        return node

    def hard_link(self, parent, name, node):
        self.link(parent, name, node)
        node.links += 1

    # 目錄項目。 / Directory entries.
    def file_type(self, node):
        if not self.filetype:
            return 0
        return {S_IFREG: 1, S_IFDIR: 2, S_IFCHR: 3, S_IFLNK: 7}[node.kind]

    def entry(self, number, name, node, record):
        if self.filetype:
            head = struct.pack('<IHBB', number, record, len(name), self.file_type(node) if node else 0)
        else:
            head = struct.pack('<IHH', number, record, len(name))
        return (head + name).ljust(record, b'\0')

    def entry_length(self, name):
        return (8 + len(name) + 3) & ~3

    def leaf_blocks(self, directory, entries):
        usable = self.block - (12 if self.csum else 0)
        blocks, current = [], []
        used = 0
        for name, node, ghost in entries:
            length = self.entry_length(name)
            extra = self.entry_length(b'ghost-entry') if ghost else 0
            if used + length + extra > usable:
                blocks.append(current)
                current, used = [], 0
            current.append((name, node, ghost))
            used += length + extra
        blocks.append(current)
        while len(blocks) < directory.dir_blocks:
            blocks.append([])
        out = []
        for chunk in blocks:
            content = bytearray()
            for i, (name, node, ghost) in enumerate(chunk):
                length = self.entry_length(name)
                if ghost:
                    # 已刪除的項目：被前一個項目的 rec_len 吞掉，但位元組還在。
                    # A deleted entry: swallowed by the previous entry's
                    # rec_len, though its bytes are still there.
                    hidden = self.entry(node.number, b'ghost-entry', node, self.entry_length(b'ghost-entry'))
                    length += len(hidden)
                if i == len(chunk) - 1:
                    length = usable - len(content)
                record = self.entry(node.number, name, node, length)
                if ghost:
                    start = self.entry_length(name)
                    record = record[:start] + hidden + record[start + len(hidden):]
                content += record
            if not chunk:
                content = bytearray(self.entry(0, b'', None, usable))
            out.append(self.finish_leaf(directory, content))
        return out

    def finish_leaf(self, directory, content):
        if self.csum:
            content = bytearray(content)
            checksum = crc32c(self.inode_seed(directory), content)
            content += struct.pack('<IHBBI', 0, 12, 0, 0xDE, checksum)
        assert len(content) == self.block
        return bytes(content)

    def write_directory(self, directory):
        entries = [(b'.', directory, False), (b'..', directory.parent, False)] + directory.children
        if not directory.indexed:
            blocks = self.leaf_blocks(directory, entries)
        else:
            # htree：區塊 0 是 dx_root，偽裝成 "." 與一個佔滿整個區塊的 ".."；
            # 只有一個葉區塊時根裡只需要第 0 個索引項目，不必算任何雜湊。
            #
            # htree: block 0 is the dx_root, disguised as "." and a ".." that
            # fills the whole block. With a single leaf block the root needs
            # only index entry 0, and no hash has to be computed.
            root = bytearray(self.entry(directory.number, b'.', directory, 12))
            root += self.entry(directory.parent.number, b'..', directory.parent, self.block - 12)[:12]
            root += struct.pack('<IBBBB', 0, 1, 8, 0, 0)
            limit = (self.block - 0x20 - (8 if self.csum else 0)) // 8
            root += struct.pack('<HHI', limit, 1, 1)
            root = root.ljust(self.block, b'\0')
            if self.csum:
                tail = 0x20 + limit * 8
                checksum = crc32c(self.inode_seed(directory), bytes(root[:0x20 + 8]) + bytes(8))
                struct.pack_into('<II', root, tail, 0, checksum)
            blocks = [bytes(root)] + self.leaf_blocks(directory, directory.children)
            directory.flags |= INODE_FLAG_INDEX
        directory.size = len(blocks) * self.block
        pieces = [(0, b''.join(blocks), False)]
        self.store(directory, pieces, directory.group)

    # inode 表。 / The inode table.
    def write_inode(self, node):
        raw = bytearray(self.inode_size)
        struct.pack_into('<HHIIIIIHHII', raw, 0, node.mode, 0, node.size & 0xFFFFFFFF, TIMESTAMP,
                         TIMESTAMP, TIMESTAMP, 0, 0, node.links, node.blocks512, node.flags)
        raw[40:100] = node.block
        struct.pack_into('<I', raw, 108, node.size >> 32)
        if self.inode_size > 128:
            struct.pack_into('<H', raw, 128, 32)
        if self.csum:
            checksum = crc32c(self.inode_seed(node), raw)
            struct.pack_into('<H', raw, 0x7C, checksum & 0xFFFF)
            struct.pack_into('<H', raw, 0x82, checksum >> 16)
        group = (node.number - 1) // self.inodes_per_group
        index = (node.number - 1) % self.inodes_per_group
        at = self.group_meta[group]['inode_table'] * self.block + index * self.inode_size
        self.image[at:at + self.inode_size] = raw

    def finish(self):
        for node in list(self.nodes.values()):
            if node.kind == S_IFDIR:
                self.write_directory(node)
        for node in self.nodes.values():
            self.write_inode(node)

        descriptors = []
        free_blocks_total = free_inodes_total = 0
        for group in range(self.groups):
            meta = self.group_meta[group]
            first = self.first_block(group)
            count = min(self.per_group, self.blocks - first)
            block_bitmap = bytearray(self.block)
            free_blocks = 0
            for i in range(self.block * 8):
                if i >= count or first + i in self.used_blocks:
                    block_bitmap[i // 8] |= 1 << (i % 8)
                else:
                    free_blocks += 1
            inode_bitmap = bytearray(self.block)
            free_inodes = 0
            directories = 0
            for i in range(self.block * 8):
                number = group * self.inodes_per_group + i + 1
                if i >= self.inodes_per_group or number in self.used_inodes:
                    inode_bitmap[i // 8] |= 1 << (i % 8)
                else:
                    free_inodes += 1
                if i < self.inodes_per_group and number in self.nodes and self.nodes[number].kind == S_IFDIR:
                    directories += 1
            self.image[meta['block_bitmap'] * self.block:(meta['block_bitmap'] + 1) * self.block] = block_bitmap
            self.image[meta['inode_bitmap'] * self.block:(meta['inode_bitmap'] + 1) * self.block] = inode_bitmap
            free_blocks_total += free_blocks
            free_inodes_total += free_inodes

            descriptor = bytearray(self.desc_size)
            struct.pack_into('<IIIHHH', descriptor, 0, meta['block_bitmap'], meta['inode_bitmap'],
                             meta['inode_table'], free_blocks & 0xFFFF, free_inodes & 0xFFFF, directories)
            if self.csum:
                block_sum = crc32c(self.seed, block_bitmap[:self.per_group // 8])
                inode_sum = crc32c(self.seed, inode_bitmap[:self.inodes_per_group // 8])
                struct.pack_into('<HH', descriptor, 0x18, block_sum & 0xFFFF, inode_sum & 0xFFFF)
                if self.desc_size >= 64:
                    struct.pack_into('<HH', descriptor, 0x38, block_sum >> 16, inode_sum >> 16)
                checksum = crc32c(crc32c(self.seed, struct.pack('<I', group)), descriptor) & 0xFFFF
                struct.pack_into('<H', descriptor, 0x1E, checksum)
            descriptors.append(bytes(descriptor))

        per_block = self.block // self.desc_size
        for group in range(self.groups):
            for at, meta, count in self.group_meta[group]['descriptors']:
                table = b''.join(descriptors[meta * per_block:(meta + count) * per_block])
                self.image[at * self.block:at * self.block + len(table)] = table

        for group in range(self.groups):
            if self.has_super(group):
                at = 1024 if group == 0 else self.first_block(group) * self.block
                self.image[at:at + 1024] = self.superblock(group, free_blocks_total, free_inodes_total)

    def superblock(self, group, free_blocks, free_inodes):
        sb = bytearray(1024)
        log = self.block.bit_length() - 11
        struct.pack_into('<IIIIIIIIIIIIIHHHHHHIIIIHHIHHIII', sb, 0,
                         self.groups * self.inodes_per_group, self.blocks & 0xFFFFFFFF, 0,
                         free_blocks & 0xFFFFFFFF, free_inodes, self.first_data_block, log, log,
                         self.per_group, self.per_group, self.inodes_per_group, 0, TIMESTAMP,
                         0, 0xFFFF, 0xEF53, 1, 1, 0, TIMESTAMP, 0, 0, 1, 0, 0,
                         FIRST_INODE, self.inode_size, group, self.compat, self.incompat,
                         self.ro_compat)
        sb[104:120] = self.uuid
        sb[120:136] = self.label.ljust(16, b'\0')
        sb[236:252] = bytes(reversed(self.uuid))
        sb[252] = 1
        struct.pack_into('<H', sb, 254, self.desc_size if self.desc_size > 32 else 0)
        struct.pack_into('<II', sb, 260, 0, TIMESTAMP)
        struct.pack_into('<I', sb, 336, self.blocks >> 32)
        if self.inode_size > 128:
            struct.pack_into('<HH', sb, 348, 32, 32)
        if self.csum:
            sb[373] = 1
            struct.pack_into('<I', sb, 1020, crc32c(0xFFFFFFFF, sb[:1020]))
        return bytes(sb)


def build_ext4(meta_bg):
    volume = Volume(block=1024, blocks=2048, per_group=64, inodes_per_group=32, inode_size=256,
                    compat=COMPAT_EXT_ATTR | COMPAT_DIR_INDEX,
                    incompat=INCOMPAT_FILETYPE | INCOMPAT_EXTENTS | INCOMPAT_64BIT |
                    (INCOMPAT_META_BG if meta_bg else 0),
                    ro_compat=RO_SPARSE_SUPER | RO_LARGE_FILE | RO_HUGE_FILE | RO_DIR_NLINK |
                    RO_EXTRA_ISIZE | RO_METADATA_CSUM,
                    uuid=bytes(range(0x10, 0x20)), label=b'shirley-ext4')
    root = volume.root
    volume.mkdir(root, b'lost+found', 0, blocks=2)
    hello = volume.file(root, b'hello.txt', b'Hello from ext4!\n')
    # 已刪除的項目藏在 hello.txt 的 rec_len 裡。 / A deleted entry hides in
    # hello.txt's rec_len.
    root.children[-1] = (b'hello.txt', hello, True)

    docs = volume.mkdir(root, b'docs', 7)
    volume.file(docs, b'readme.md', pattern(3000, 7), group=7)
    nested = volume.mkdir(docs, b'nested', 20)
    deep = volume.mkdir(nested, b'deep', 20)
    volume.file(deep, b'leaf.txt', b'deep leaf\n', group=31)

    # 六段 extent，放不進 inode 的四個位置，所以有一個葉節點區塊。
    # Six extents, more than the inode's four slots, so there is a leaf block.
    block = 1024
    size = 32 * block - 100
    whole = pattern(size, 13)
    layout = [(0, 3, False), (3, 2, False), (8, 2, True), (12, 1, False), (20, 4, False), (30, 2, False)]
    pieces = []
    for logical, count, uninitialized in reversed(layout):
        pieces.append((logical, whole[logical * block:(logical + count) * block], uninitialized))
    volume.file(root, b'fragmented.bin', group=3, pieces=pieces, size=size)

    volume.file(root, b'sparse.bin', group=12, pieces=[(5000, pattern(block, 3), False)],
                size=10 * 1024 * 1024)
    volume.file(root, b'empty.txt')
    volume.file(root, '名前-ünïcødé.txt'.encode('utf-8'), b'unicode\n')
    volume.file(root, b'L' * 255, b'long name\n')
    volume.symlink(root, b'link-fast', b'docs/readme.md')
    volume.symlink(root, b'link-slow', b'docs/' + b'x' * 95, group=2)
    volume.device(root, b'null', 1, 3)

    shared = volume.file(root, b'shared.txt', b'shared\n', group=1)
    many = volume.mkdir(root, b'many', 5)
    for i in range(80):
        volume.hard_link(many, ('entry-%02d-with-a-longer-name.txt' % i).encode(), shared)
    indexed = volume.mkdir(root, b'indexed', 6)
    indexed.indexed = True
    for i in range(5):
        volume.hard_link(indexed, ('indexed-%d' % i).encode(), shared)
    volume.finish()
    return volume.image


def build_ext2():
    volume = Volume(block=2048, blocks=512, per_group=256, inodes_per_group=32, inode_size=128,
                    compat=0, incompat=0, ro_compat=RO_SPARSE_SUPER,
                    uuid=bytes(range(0x30, 0x40)), label=b'shirley-ext2')
    root = volume.root
    volume.mkdir(root, b'lost+found', 0, blocks=2)
    volume.file(root, b'hello.txt', b'Hello from ext2!\n')
    block = 2048
    volume.file(root, b'blocks.bin', pattern(40 * block - 5, 11), group=0)

    # 直接、一、二、三層間接區塊各放一個區塊。 / One block behind each of the
    # direct, single, double and triple indirect pointers.
    per = block // 4
    positions = [0, 12, 12 + per, 12 + per + per * per]
    pieces = [(logical, bytes([index + 1]) * block, False) for index, logical in enumerate(positions)]
    volume.file(root, b'sparse.bin', group=1, pieces=pieces, size=(positions[-1] + 1) * block)

    directory = volume.mkdir(root, b'dir', 1)
    sub = volume.mkdir(directory, b'sub', 1)
    volume.file(sub, b'file.txt', b'nested\n', group=1)
    volume.symlink(root, b'link', b'hello.txt')
    volume.finish()
    return volume.image


def main():
    options = {'--ext2', '--meta-bg'}
    arguments = [argument for argument in sys.argv[1:] if argument not in options]
    ext2 = '--ext2' in sys.argv[1:]
    meta_bg = '--meta-bg' in sys.argv[1:]
    default = 'ext2-basic.img' if ext2 else 'ext4-metabg.img' if meta_bg else 'ext4-basic.img'
    output = arguments[0] if arguments else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', 'tests', 'images', default)
    image = build_ext2() if ext2 else build_ext4(meta_bg)
    with open(output, 'wb') as handle:
        handle.write(image)
    print('wrote', os.path.normpath(output), len(image), 'bytes')


if __name__ == '__main__':
    main()
