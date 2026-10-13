#!/usr/bin/env python3
# exFAT 映像的一致性檢查，一個小型的 fsck。依照 Microsoft 的 exFAT 規格獨立撰寫，
# 不與核心共用任何程式碼：核心寫過的映像交給它檢查，兩邊對規格的理解不一致時
# 才會失敗。
#
# 檢查開機校驗和、大寫表校驗和、每組目錄項目的校驗和與名稱雜湊、名稱合法性、
# 目錄結束標記之後沒有使用中的項目、FAT 鏈的長度與結尾、叢集沒有被兩個擁有者
# 共用，以及配置位元圖與實際使用的叢集完全一致（沒有洩漏，也沒有漏標）。
#
# 用法：python tools/check-exfat-image.py <映像> [--tree] [--clean]
#   --tree   列出每個檔案與目錄：路徑、大小、內容的 CRC-32
#   --clean  另外要求 VolumeDirty 已經清掉（卷冊被乾淨地卸載過）
#
# A consistency check for exFAT images: a small fsck. Written independently
# from Microsoft's exFAT specification and sharing no code with the kernel:
# images the kernel has written are handed to it, and it fails only when the
# two understand the specification differently.
#
# It checks the boot checksum, the up-case table checksum, every directory
# entry set's checksum and name hash, legal names, that no entry in use follows
# an end-of-directory marker, FAT chain lengths and terminators, that no
# cluster has two owners, and that the allocation bitmap matches the clusters
# in use exactly (nothing leaked, nothing missing).
#
# Usage: python tools/check-exfat-image.py <image> [--tree] [--clean]
#   --tree   list every file and directory: path, size, CRC-32 of the content
#   --clean  also require VolumeDirty to be clear (cleanly unmounted)

import struct
import sys
import zlib

END_OF_CHAIN = 0xFFFFFFFF
ILLEGAL = set('"*/:<>?\\|')


class Check:
    def __init__(self, path):
        with open(path, 'rb') as handle:
            self.image = handle.read()
        self.errors = []
        self.owners = {}
        self.tree = []

    def fail(self, message):
        self.errors.append(message)

    def u16(self, offset):
        return struct.unpack_from('<H', self.image, offset)[0]

    def u32(self, offset):
        return struct.unpack_from('<I', self.image, offset)[0]

    def u64(self, offset):
        return struct.unpack_from('<Q', self.image, offset)[0]

    def boot(self):
        image = self.image
        if image[3:11] != b'EXFAT   ' or image[510:512] != b'\x55\xaa':
            raise SystemExit('not an exFAT image')
        self.sector = 1 << image[108]
        self.cluster = self.sector << image[109]
        self.fat = self.u32(80) * self.sector
        self.heap = self.u32(88) * self.sector
        self.count = self.u32(92)
        self.root = self.u32(96)
        self.flags = self.u16(106)
        if image[110] != 1:
            self.fail('expected one FAT, found %d' % image[110])
        total = 0
        for index in range(11 * self.sector):
            if index in (106, 107, 112):
                continue
            total = ((((total & 1) << 31) | (total >> 1)) + image[index]) & 0xFFFFFFFF
        for offset in range(11 * self.sector, 12 * self.sector, 4):
            if self.u32(offset) != total:
                self.fail('boot checksum sector does not match')
                break

    def next(self, cluster):
        return self.u32(self.fat + cluster * 4)

    def valid(self, cluster):
        return 2 <= cluster <= self.count + 1

    def clusters(self, first, length, contiguous, owner):
        """一串叢集，並登記擁有者。 / A run of clusters, with its owner recorded."""
        needed = (length + self.cluster - 1) // self.cluster
        if needed == 0:
            if first != 0:
                self.fail('%s: empty but first cluster is %d' % (owner, first))
            return []
        result = []
        cluster = first
        for index in range(needed):
            if not self.valid(cluster):
                self.fail('%s: cluster %d out of range' % (owner, cluster))
                return result
            result.append(cluster)
            if cluster in self.owners:
                self.fail('%s: cluster %d already belongs to %s' % (owner, cluster, self.owners[cluster]))
            self.owners[cluster] = owner
            if contiguous:
                cluster += 1
            elif index + 1 < needed:
                cluster = self.next(cluster)
        if not contiguous and self.next(result[-1]) != END_OF_CHAIN:
            self.fail('%s: chain does not end after %d clusters' % (owner, needed))
        return result

    def chain_length(self, first):
        length = 0
        cluster = first
        while True:
            length += 1
            following = self.next(cluster)
            if following == END_OF_CHAIN:
                return length
            if not self.valid(following) or length > self.count:
                self.fail('root: broken chain')
                return length
            cluster = following

    def read(self, clusters, length):
        data = bytearray()
        for cluster in clusters:
            start = self.heap + (cluster - 2) * self.cluster
            data += self.image[start:start + self.cluster]
        return bytes(data[:length])

    def load_upcase(self, first, length, checksum):
        data = self.read(self.clusters(first, length, False, 'up-case table'), length)
        total = 0
        for byte in data:
            total = ((((total & 1) << 31) | (total >> 1)) + byte) & 0xFFFFFFFF
        if total != checksum:
            self.fail('up-case table checksum does not match')
        self.upcase = list(range(65536))
        units = struct.unpack('<%dH' % (len(data) // 2), data)
        index = 0
        position = 0
        while position < len(units):
            if units[position] == 0xFFFF and position + 1 < len(units):
                index += units[position + 1]
                position += 2
                continue
            if index < 65536:
                self.upcase[index] = units[position]
            index += 1
            position += 1

    def directory(self, data, path, is_root):
        entries = [data[i:i + 32] for i in range(0, len(data), 32)]
        ended = False
        index = 0
        while index < len(entries):
            entry = entries[index]
            kind = entry[0]
            if ended:
                if kind & 0x80:
                    self.fail('%s: entry %d in use after the end-of-directory marker' % (path, index))
                index += 1
                continue
            if kind == 0x00:
                ended = True
                index += 1
                continue
            if not kind & 0x80:
                index += 1
                continue
            if is_root and kind in (0x81, 0x82, 0x83):
                index += 1
                continue
            if kind != 0x85:
                if not kind & 0x40:
                    self.fail('%s: unexpected primary entry 0x%02x at %d' % (path, kind, index))
                index += 1
                continue
            secondary = entry[1]
            group = entries[index:index + 1 + secondary]
            if secondary < 2 or len(group) != secondary + 1:
                self.fail('%s: entry set at %d is truncated' % (path, index))
                index += 1
                continue
            raw = b''.join(group)
            total = 0
            for position, byte in enumerate(raw):
                if position in (2, 3):
                    continue
                total = ((((total & 1) << 15) | (total >> 1)) + byte) & 0xFFFF
            if total != struct.unpack_from('<H', raw, 2)[0]:
                self.fail('%s: entry set at %d has a bad checksum' % (path, index))
            stream = group[1]
            if stream[0] != 0xC0:
                self.fail('%s: entry set at %d has no stream extension' % (path, index))
                index += 1 + secondary
                continue
            name_length = stream[3]
            units = []
            for part in group[2:]:
                if part[0] != 0xC1:
                    if part[0] & 0xC0 != 0xC0:
                        self.fail('%s: entry set at %d has a bad secondary' % (path, index))
                    continue
                units += struct.unpack_from('<15H', part, 2)
            units = units[:name_length]
            if len(units) != name_length or name_length == 0:
                self.fail('%s: entry set at %d has a short name' % (path, index))
            name = struct.pack('<%dH' % len(units), *units).decode('utf-16-le', 'surrogatepass')
            if name in ('.', '..') or any(c in ILLEGAL or ord(c) < 0x20 for c in name):
                self.fail('%s: illegal name %r' % (path, name))
            hashed = 0
            for unit in units:
                upper = self.upcase[unit]
                for byte in (upper & 0xFF, upper >> 8):
                    hashed = ((((hashed & 1) << 15) | (hashed >> 1)) + byte) & 0xFFFF
            if hashed != struct.unpack_from('<H', stream, 4)[0]:
                self.fail('%s/%s: name hash does not match' % (path, name))
            attributes = struct.unpack_from('<H', group[0], 4)[0]
            flags = stream[1]
            valid = struct.unpack_from('<Q', stream, 8)[0]
            first = struct.unpack_from('<I', stream, 20)[0]
            length = struct.unpack_from('<Q', stream, 24)[0]
            child = (path.rstrip('/') + '/' + name)
            if valid > length:
                self.fail('%s: ValidDataLength %d exceeds DataLength %d' % (child, valid, length))
            clusters = self.clusters(first, length, bool(flags & 0x02), child)
            if attributes & 0x10:
                if length == 0 or length % self.cluster:
                    self.fail('%s: directory length %d is not whole clusters' % (child, length))
                self.tree.append('%s/ dir' % child)
                self.directory(self.read(clusters, length), child, False)
            else:
                content = self.read(clusters, valid) + bytes(length - valid)
                self.tree.append('%s %d %08x' % (child, length, zlib.crc32(content)))
            index += 1 + secondary

    def run(self, clean):
        self.boot()
        if clean and self.flags & 0x02:
            self.fail('VolumeDirty is still set')
        root_length = self.chain_length(self.root) * self.cluster
        root_clusters = self.clusters(self.root, root_length, False, '/')
        root = self.read(root_clusters, root_length)
        bitmap = upcase = None
        for offset in range(0, len(root), 32):
            kind = root[offset]
            if kind == 0:
                break
            if kind == 0x81 and bitmap is None:
                bitmap = (struct.unpack_from('<I', root, offset + 20)[0], struct.unpack_from('<Q', root, offset + 24)[0])
            if kind == 0x82 and upcase is None:
                upcase = (struct.unpack_from('<I', root, offset + 20)[0], struct.unpack_from('<Q', root, offset + 24)[0],
                          struct.unpack_from('<I', root, offset + 4)[0])
        if bitmap is None or upcase is None:
            raise SystemExit('the root directory lacks the bitmap or the up-case table')
        self.load_upcase(*upcase)
        bitmap_clusters = self.clusters(bitmap[0], bitmap[1], False, 'allocation bitmap')
        bits = self.read(bitmap_clusters, bitmap[1])
        self.directory(root, '/', True)
        for index in range(self.count):
            cluster = index + 2
            marked = bool(bits[index // 8] >> (index % 8) & 1)
            owned = cluster in self.owners
            if marked and not owned:
                self.fail('cluster %d is marked used but nothing owns it' % cluster)
            elif owned and not marked:
                self.fail('cluster %d belongs to %s but is marked free' % (cluster, self.owners[cluster]))
        return self.errors


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__ or 'usage: check-exfat-image.py <image> [--tree] [--clean]')
    check = Check(args[0])
    errors = check.run('--clean' in args)
    if '--tree' in args:
        for line in check.tree:
            print(line)
    for error in errors:
        print('error: ' + error, file=sys.stderr)
    if errors:
        raise SystemExit(1)
    print('%s: clean, %d clusters in use of %d' % (args[0], len(check.owners), check.count))


if __name__ == '__main__':
    main()
