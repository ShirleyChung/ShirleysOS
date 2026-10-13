#include "shirley/checksum.hpp"
#include "shirley/device.hpp"
#include "shirley/heap.hpp"
#include "shirley/partition.hpp"
#include "shirley/ram_disk.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <tuple>
#include <vector>

namespace {

using namespace shirley;

alignas(4096) unsigned char arena[64 * 1024];

void put32(std::vector<std::uint8_t>& disk, std::size_t offset, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) disk[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
void put64(std::vector<std::uint8_t>& disk, std::size_t offset, std::uint64_t value) {
    put32(disk, offset, static_cast<std::uint32_t>(value));
    put32(disk, offset + 4, static_cast<std::uint32_t>(value >> 32));
}

void mbr_entry(std::vector<std::uint8_t>& disk, std::size_t sector, int slot, std::uint8_t status,
               std::uint8_t type, std::uint32_t start, std::uint32_t count) {
    const auto base = sector * 512 + 446 + slot * 16;
    disk[base] = status;
    disk[base + 4] = type;
    put32(disk, base + 8, start);
    put32(disk, base + 12, count);
    disk[sector * 512 + 510] = 0x55;
    disk[sector * 512 + 511] = 0xAA;
}

void test_crc32() {
    // 標準測試向量。 / The standard check value.
    const char text[] = "123456789";
    assert(checksum::crc32(text, 9) == 0xCBF43926u);
    // 分段計算等於一次算完。 / Pieces chain to the same value.
    assert(checksum::crc32(text + 4, 5, checksum::crc32(text, 4)) == 0xCBF43926u);
    assert(checksum::crc32(text, 0) == 0);
}

// 主要分割區 1 與 4、延伸分割區 3 裡的兩個邏輯分割區（編號 5、6）。
// Primaries 1 and 4, plus two logical partitions (numbers 5 and 6) inside
// extended partition 3.
std::vector<std::uint8_t> mbr_disk() {
    std::vector<std::uint8_t> disk(128 * 512);
    mbr_entry(disk, 0, 0, 0x80, 0x07, 2, 10);
    mbr_entry(disk, 0, 2, 0x00, 0x0F, 20, 60);
    mbr_entry(disk, 0, 3, 0x00, 0x83, 100, 20);
    // 第一個 EBR：邏輯分割區相對於 EBR 自己，下一個 EBR 相對於延伸分割區開頭。
    // First EBR: the logical partition is relative to the EBR itself, the next
    // EBR relative to the start of the extended partition.
    mbr_entry(disk, 20, 0, 0x00, 0x0B, 2, 8);
    mbr_entry(disk, 20, 1, 0x00, 0x05, 20, 10);
    mbr_entry(disk, 40, 0, 0x00, 0x0C, 1, 5);
    return disk;
}

void test_mbr() {
    auto image = mbr_disk();
    io::RamDisk disk(image.data(), image.size());
    partition::Table table;
    assert(partition::read(disk, table));
    assert(table.scheme == partition::Scheme::Mbr && table.count == 4 && !table.truncated);
    const std::uint32_t numbers[] = {1, 5, 6, 4};
    const std::uint64_t firsts[] = {2, 22, 41, 100};
    const std::uint64_t counts[] = {10, 8, 5, 20};
    const std::uint8_t types[] = {0x07, 0x0B, 0x0C, 0x83};
    for (std::size_t i = 0; i < 4; ++i) {
        assert(table.entries[i].number == numbers[i]);
        assert(table.entries[i].first == firsts[i] && table.entries[i].count == counts[i]);
        assert(table.entries[i].mbr_type == types[i]);
    }

    // EBR 鏈指回自己也會結束。 / An EBR chain pointing back at itself still
    // ends.
    mbr_entry(image, 40, 1, 0x00, 0x05, 20, 10);
    io::RamDisk looping(image.data(), image.size());
    assert(partition::read(looping, table) && table.truncated);
}

void test_not_a_table() {
    std::vector<std::uint8_t> image(64 * 512);
    io::RamDisk blank(image.data(), image.size());
    partition::Table table;
    assert(partition::read(blank, table) && table.scheme == partition::Scheme::None);

    // 整顆格式化成 exFAT 的磁碟：開機磁區也以 0x55AA 結尾，但那不是分割表。
    // A disk formatted as exFAT end to end: its boot sector also ends in 0x55AA,
    // but that is no partition table.
    std::memcpy(image.data() + 3, "EXFAT   ", 8);
    mbr_entry(image, 0, 0, 0x00, 0x07, 2, 10);
    io::RamDisk exfat(image.data(), image.size());
    assert(partition::read(exfat, table) && table.scheme == partition::Scheme::None);

    // 狀態位元組不是 0 或 0x80，或分割區超出磁碟：也不是分割表。
    // A status byte other than 0 or 0x80, or a partition past the end of the
    // disk: no partition table either.
    std::fill(image.begin(), image.end(), 0);
    mbr_entry(image, 0, 0, 0x37, 0x07, 2, 10);
    io::RamDisk status(image.data(), image.size());
    assert(partition::read(status, table) && table.scheme == partition::Scheme::None);
    std::fill(image.begin(), image.end(), 0);
    mbr_entry(image, 0, 0, 0x00, 0x07, 2, 1000);
    io::RamDisk beyond(image.data(), image.size());
    assert(partition::read(beyond, table) && table.scheme == partition::Scheme::None);
}

// 256 個區塊的 GPT 磁碟：32 個 128 位元組的項目佔 8 個區塊。主要表在區塊 1 與
// 2..9，備份表在 246..253 與最後一個區塊。
//
// A 256-block GPT disk: 32 entries of 128 bytes fill 8 blocks. The primary
// copy is block 1 plus 2..9, the backup is 246..253 plus the last block.
constexpr std::uint64_t gpt_blocks = 256;

void gpt_header(std::vector<std::uint8_t>& disk, std::uint64_t lba, std::uint64_t other,
                std::uint64_t array_lba) {
    const auto base = lba * 512;
    std::memset(disk.data() + base, 0, 512);
    std::memcpy(disk.data() + base, "EFI PART", 8);
    put32(disk, base + 8, 0x00010000);
    put32(disk, base + 12, 92);
    put64(disk, base + 24, lba);
    put64(disk, base + 32, other);
    put64(disk, base + 40, 34);
    put64(disk, base + 48, gpt_blocks - 34);
    put64(disk, base + 72, array_lba);
    put32(disk, base + 80, 32);
    put32(disk, base + 84, 128);
    put32(disk, base + 88, checksum::crc32(disk.data() + array_lba * 512, 32 * 128));
    put32(disk, base + 16, checksum::crc32(disk.data() + base, 92));
}

std::vector<std::uint8_t> gpt_disk() {
    std::vector<std::uint8_t> disk(gpt_blocks * 512);
    mbr_entry(disk, 0, 0, 0x00, 0xEE, 1, 0xFFFFFFFF);
    const std::uint8_t basic_data[16] = {0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
                                         0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7};
    for (const std::uint64_t array : {std::uint64_t{2}, gpt_blocks - 10}) {
        // 項目 0 與項目 2；項目 1 空著。 / Entries 0 and 2; entry 1 is unused.
        for (const auto& [index, first, last] :
             {std::tuple<int, std::uint64_t, std::uint64_t>{0, 34, 63}, {2, 64, 99}}) {
            const auto base = array * 512 + index * 128;
            std::memcpy(disk.data() + base, basic_data, 16);
            disk[base + 16] = static_cast<std::uint8_t>(index + 1);
            put64(disk, base + 32, first);
            put64(disk, base + 40, last);
        }
    }
    gpt_header(disk, 1, gpt_blocks - 1, 2);
    gpt_header(disk, gpt_blocks - 1, 1, gpt_blocks - 10);
    return disk;
}

void check_gpt(const partition::Table& table) {
    assert(table.scheme == partition::Scheme::Gpt && table.count == 2);
    assert(table.entries[0].number == 1 && table.entries[0].first == 34 && table.entries[0].count == 30);
    assert(table.entries[1].number == 3 && table.entries[1].first == 64 && table.entries[1].count == 36);
    assert(table.entries[0].type_guid[0] == 0xA2 && table.entries[0].mbr_type == 0);
}

void test_gpt() {
    auto image = gpt_disk();
    partition::Table table;
    io::RamDisk good(image.data(), image.size());
    assert(partition::read(good, table));
    check_gpt(table);
    assert(!table.used_backup);

    // 主要標頭壞了：改用備份。 / The primary header damaged: the backup is used.
    auto damaged = image;
    damaged[512 + 40] ^= 0x01;
    io::RamDisk header(damaged.data(), damaged.size());
    assert(partition::read(header, table));
    check_gpt(table);
    assert(table.used_backup);

    // 主要項目表壞了（標頭的 CRC 仍然對）：同樣改用備份。
    // The primary entry array damaged while its header's CRC still holds: the
    // backup is used as well.
    damaged = image;
    damaged[2 * 512 + 40] ^= 0x01;
    io::RamDisk array(damaged.data(), damaged.size());
    assert(partition::read(array, table) && table.used_backup);
    check_gpt(table);

    // 兩份都壞了：這是一張壞掉的分割表，不是沒有分割表。
    // Both copies damaged: this is a broken table, not an absent one.
    damaged[(gpt_blocks - 1) * 512 + 40] ^= 0x01;
    io::RamDisk both(damaged.data(), damaged.size());
    assert(!partition::read(both, table) && table.count == 0);
}

void test_window() {
    std::vector<std::uint8_t> image(16 * 512);
    io::RamDisk disk(image.data(), image.size());
    io::Partition window;
    assert(window.block_count() == 0);
    window.attach(disk, 4, 6);
    assert(window.block_size() == 512 && window.block_count() == 6);
    std::uint8_t block[512];
    std::memset(block, 0x5A, sizeof(block));
    assert(window.write_blocks(0, 1, block).transferred == 512);
    assert(image[4 * 512] == 0x5A && image[3 * 512 + 511] == 0);
    assert(window.read_blocks(5, 1, block));
    assert(window.read_blocks(5, 2, block).error == io::Error::OutOfRange);
    assert(window.write_blocks(6, 1, block).error == io::Error::OutOfRange);
}

void test_publish() {
    device::initialize();
    auto mbr = mbr_disk();
    io::RamDisk mbr_storage(mbr.data(), mbr.size());
    device::Device vda{"vda", device::Type::Block, device::block_operations, &mbr_storage};
    auto gpt = gpt_disk();
    io::RamDisk gpt_storage(gpt.data(), gpt.size());
    device::Device ram{"ram7", device::Type::Block, device::block_operations, &gpt_storage};
    assert(device::register_device(vda) == device::Status::Ok);
    assert(device::register_device(ram) == device::Status::Ok);

    assert(partition::publish(vda) == 4);
    // 名字以數字結尾的磁碟，分割區編號前面加 p。 / A disk name ending in a
    // digit gets a p before the partition number.
    assert(partition::publish(ram) == 2);
    for (const char* name : {"vda1", "vda4", "vda5", "vda6", "ram7p1", "ram7p3"})
        assert(device::find(name) != nullptr);
    assert(device::find("vda2") == nullptr && device::find("ram71") == nullptr);

    auto* logical = device::find("vda5");
    assert(logical->block_count() == 8 && logical->block_size() == 512);
    std::uint8_t block[512];
    mbr[22 * 512] = 0xC3;
    assert(logical->block_read(0, 1, block) && block[0] == 0xC3);

    // 同一顆磁碟再發布一次，名字重複的分割區不會被登記兩次。
    // Publishing the same disk again registers no partition twice.
    assert(partition::publish(vda) == 0);

    partition::unpublish(vda);
    assert(device::find("vda1") == nullptr && device::find("ram7p1") != nullptr);
    partition::unpublish(ram);
    assert(device::find("ram7p3") == nullptr);
    device::unregister_device(ram);
    device::unregister_device(vda);
}

} // namespace

int main() {
    heap::initialize(arena, sizeof(arena));
    test_crc32();
    test_mbr();
    test_not_a_table();
    test_gpt();
    test_window();
    test_publish();
    assert(heap::available() == heap::capacity());
    return 0;
}
