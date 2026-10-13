#include "shirley/partition.hpp"

#include "shirley/checksum.hpp"
#include "shirley/format.hpp"
#include "shirley/heap.hpp"
#include "shirley/text.hpp"

#include <limits>

namespace shirley::io {

void Partition::attach(BlockDevice& disk, std::uint64_t first, std::uint64_t count) {
    disk_ = &disk;
    first_ = first;
    count_ = count;
}

std::size_t Partition::block_size() const { return disk_ != nullptr ? disk_->block_size() : 0; }

Result Partition::read_blocks(std::uint64_t first, std::size_t count, void* buffer) {
    if (count != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    if (disk_ == nullptr) return {0, Error::DeviceError};
    if (first > count_ || count > count_ - first) return {0, Error::OutOfRange};
    return disk_->read_blocks(first_ + first, count, buffer);
}

Result Partition::write_blocks(std::uint64_t first, std::size_t count, const void* buffer) {
    if (count != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    if (disk_ == nullptr) return {0, Error::DeviceError};
    if (first > count_ || count > count_ - first) return {0, Error::OutOfRange};
    return disk_->write_blocks(first_ + first, count, buffer);
}

} // namespace shirley::io

namespace shirley::partition {
namespace {

std::uint32_t le32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}
std::uint64_t le64(const std::uint8_t* bytes) {
    return le32(bytes) | (static_cast<std::uint64_t>(le32(bytes + 4)) << 32);
}

constexpr std::size_t mbr_table = 446;
constexpr std::size_t mbr_entry_bytes = 16;
constexpr std::uint8_t mbr_type_protective = 0xEE;
// 延伸分割區鏈最多走這麼多層。鏈指回自己時迴圈因此一定會結束。
// How far the extended partition chain is followed at most, so a chain that
// points back at itself still ends.
constexpr std::size_t max_logical = 64;
// GPT 項目表的大小上限。規格的最小值是 16 KiB；超過 1 MiB 的表不是合理的磁碟。
// The cap on a GPT entry array. The specification's minimum is 16 KiB; a table
// past 1 MiB is not a plausible disk.
constexpr std::uint64_t max_gpt_array = 1024 * 1024;

bool extended_type(std::uint8_t type) { return type == 0x05 || type == 0x0F || type == 0x85; }

bool has_signature(const std::uint8_t* block) { return block[510] == 0x55 && block[511] == 0xAA; }

// 0x55AA 不只出現在 MBR：FAT、exFAT 與 NTFS 的開機磁區也以它結尾。整顆磁碟
// 格式化成一個檔案系統時，第 0 個區塊就是那種開機磁區，446 開始的位元組是
// 開機程式而不是分割表，不能當成分割區來讀。
//
// 0x55AA ends more than an MBR: the boot sectors of FAT, exFAT and NTFS end
// with it too. When a whole disk is formatted as one file system, block 0 is
// such a boot sector and the bytes from 446 on are boot code, not a partition
// table, and must not be read as partitions.
bool file_system_boot_sector(const std::uint8_t* block) {
    const auto at = [block](std::size_t offset, const char* text) {
        for (std::size_t i = 0; text[i] != '\0'; ++i)
            if (block[offset + i] != static_cast<std::uint8_t>(text[i])) return false;
        return true;
    };
    return at(3, "EXFAT   ") || at(3, "NTFS    ") || at(54, "FAT") || at(82, "FAT32");
}

bool add(Table& table, std::uint32_t number, std::uint64_t first, std::uint64_t count,
         std::uint8_t mbr_type, const std::uint8_t* guid) {
    if (table.count == max_entries) {
        table.truncated = true;
        return false;
    }
    Entry& entry = table.entries[table.count++];
    entry = Entry{};
    entry.number = number;
    entry.first = first;
    entry.count = count;
    entry.mbr_type = mbr_type;
    if (guid != nullptr)
        for (std::size_t i = 0; i < 16; ++i) entry.type_guid[i] = guid[i];
    return true;
}

// 走延伸分割區裡的 EBR 鏈。每個 EBR 的第一個項目是一個邏輯分割區（相對於這個
// EBR），第二個項目指向下一個 EBR（相對於延伸分割區的開頭）。
//
// Walk the EBR chain inside an extended partition. Each EBR's first entry is a
// logical partition, relative to that EBR, and its second entry points at the
// next EBR, relative to the start of the extended partition.
bool read_logical(io::BlockDevice& disk, std::uint8_t* block, std::uint64_t base,
                  std::uint64_t length, Table& table) {
    std::uint64_t ebr = base;
    std::uint32_t number = 5;
    for (std::size_t step = 0; step < max_logical; ++step) {
        if (!disk.read_blocks(ebr, 1, block)) return false;
        if (!has_signature(block)) return true;
        const auto* logical = block + mbr_table;
        const auto* next = logical + mbr_entry_bytes;
        const auto start = static_cast<std::uint64_t>(le32(logical + 8));
        const auto count = static_cast<std::uint64_t>(le32(logical + 12));
        if (logical[4] != 0 && count != 0 && start != 0 && ebr + start + count <= base + length)
            add(table, number++, ebr + start, count, logical[4], nullptr);
        if (!extended_type(next[4]) || le32(next + 8) == 0) return true;
        const auto following = base + le32(next + 8);
        if (following >= base + length) return true;
        ebr = following;
    }
    return true;
}

// 讀一份 GPT：lba 上的標頭，以及它指向的項目表。標頭與項目表的 CRC32 都必須
// 對；不對時回傳 false，table 的內容不可用。
//
// Read one copy of the GPT: the header at lba and the entry array it points to.
// Both the header's and the array's CRC32 have to match; otherwise this returns
// false and table must not be used.
bool read_gpt(io::BlockDevice& disk, std::uint8_t* block, std::uint64_t lba, Table& table) {
    const auto block_bytes = disk.block_size();
    const auto blocks = disk.block_count();
    if (!disk.read_blocks(lba, 1, block)) return false;
    const char signature[] = "EFI PART";
    for (std::size_t i = 0; i < 8; ++i)
        if (block[i] != static_cast<std::uint8_t>(signature[i])) return false;
    const auto header_bytes = le32(block + 12);
    const auto header_crc = le32(block + 16);
    if (header_bytes < 92 || header_bytes > block_bytes || le64(block + 24) != lba) return false;
    for (std::size_t i = 16; i < 20; ++i) block[i] = 0;
    if (checksum::crc32(block, header_bytes) != header_crc) return false;

    const auto array_lba = le64(block + 72);
    const auto entry_total = le32(block + 80);
    const auto entry_bytes = le32(block + 84);
    const auto array_crc = le32(block + 88);
    // 項目大小是 128 乘上 2 的次方；要求它整除區塊大小，一個項目就不會跨區塊。
    // An entry is 128 times a power of two; requiring it to divide the block
    // size keeps every entry inside one block.
    if (entry_bytes < 128 || (entry_bytes & (entry_bytes - 1)) != 0 || block_bytes % entry_bytes != 0)
        return false;
    const auto array_bytes = static_cast<std::uint64_t>(entry_total) * entry_bytes;
    if (array_bytes == 0 || array_bytes > max_gpt_array) return false;
    const auto array_blocks = (array_bytes + block_bytes - 1) / block_bytes;
    if (array_lba >= blocks || array_blocks > blocks - array_lba) return false;

    table = Table{};
    table.scheme = Scheme::Gpt;
    std::uint32_t crc = 0;
    std::uint64_t remaining = array_bytes;
    std::uint32_t index = 0;
    for (std::uint64_t at = 0; at < array_blocks; ++at) {
        if (!disk.read_blocks(array_lba + at, 1, block)) return false;
        const auto bytes = remaining < block_bytes ? static_cast<std::size_t>(remaining) : block_bytes;
        crc = checksum::crc32(block, bytes, crc);
        remaining -= bytes;
        for (std::size_t offset = 0; offset < bytes; offset += entry_bytes, ++index) {
            const auto* entry = block + offset;
            bool used = false;
            for (std::size_t i = 0; i < 16; ++i) used = used || entry[i] != 0;
            if (!used) continue;
            const auto first = le64(entry + 32);
            const auto last = le64(entry + 40);
            if (first > last || last >= blocks) continue;
            add(table, index + 1, first, last - first + 1, 0, entry);
        }
    }
    return crc == array_crc;
}

} // namespace

const char* scheme_name(Scheme scheme) {
    switch (scheme) {
    case Scheme::None: return "none";
    case Scheme::Mbr: return "mbr";
    case Scheme::Gpt: return "gpt";
    }
    return "unknown";
}

bool read(io::BlockDevice& disk, Table& table) {
    table = Table{};
    const auto block_bytes = disk.block_size();
    const auto blocks = disk.block_count();
    if (block_bytes < 512 || block_bytes > 4096) return false;
    // 兩個區塊都不到的裝置放不下任何分割表。 / A device under two blocks
    // holds no partition table.
    if (blocks < 2) return true;
    auto* block = static_cast<std::uint8_t*>(heap::allocate(block_bytes));
    if (block == nullptr) return false;
    const auto finish = [block](bool result) {
        heap::free(block);
        return result;
    };

    if (!disk.read_blocks(0, 1, block)) return finish(false);
    if (!has_signature(block) || file_system_boot_sector(block)) return finish(true);

    // 先確定 446 開始的四個項目看起來真的是分割表：狀態位元組只能是 0 或 0x80，
    // 分割區也必須落在磁碟內。任何一項不像，就當成沒有分割表。
    //
    // First make sure the four entries from 446 really look like a partition
    // table: the status byte can only be 0 or 0x80, and each partition has to
    // lie inside the disk. If any entry does not, treat the disk as having no
    // table.
    bool protective = false;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto* entry = block + mbr_table + i * mbr_entry_bytes;
        if (entry[0] != 0x00 && entry[0] != 0x80) return finish(true);
        if (entry[4] == 0) continue;
        if (entry[4] == mbr_type_protective) protective = true;
        const auto start = static_cast<std::uint64_t>(le32(entry + 8));
        const auto count = static_cast<std::uint64_t>(le32(entry + 12));
        // 保護性 MBR 的長度常被設成 0xFFFFFFFF，不必落在磁碟內。
        // A protective MBR's length is often 0xFFFFFFFF and need not fit.
        if (entry[4] != mbr_type_protective && (start == 0 || count == 0 || start + count > blocks))
            return finish(true);
    }

    if (protective) {
        if (read_gpt(disk, block, 1, table)) return finish(true);
        // 主要標頭壞了，改讀磁碟最後一個區塊上的備份。
        // The primary header is damaged; read the backup in the disk's last
        // block instead.
        if (read_gpt(disk, block, blocks - 1, table)) {
            table.used_backup = true;
            return finish(true);
        }
        table = Table{};
        return finish(false);
    }

    table.scheme = Scheme::Mbr;
    struct Primary {
        std::uint8_t type;
        std::uint64_t start;
        std::uint64_t count;
    } primaries[4]{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto* entry = block + mbr_table + i * mbr_entry_bytes;
        primaries[i] = {entry[4], le32(entry + 8), le32(entry + 12)};
    }
    for (std::size_t i = 0; i < 4; ++i) {
        const auto& primary = primaries[i];
        if (primary.type == 0) continue;
        if (extended_type(primary.type)) {
            if (!read_logical(disk, block, primary.start, primary.count, table)) {
                table = Table{};
                return finish(false);
            }
            continue;
        }
        add(table, static_cast<std::uint32_t>(i + 1), primary.start, primary.count, primary.type,
            nullptr);
    }
    return finish(true);
}

namespace {

struct Slot {
    device::BlockView disk_view;
    io::Partition window;
    device::Device device;
    device::Device* disk = nullptr;
    bool used = false;
};

// constinit：核心不執行全域建構子。 / constinit: the kernel runs no global
// constructors.
constinit Slot slots[max_devices]{};

// 磁碟名稱加上分割區編號。名稱以數字結尾時中間加一個 p，`ram0` 的第一個分割區
// 才不會變成看起來像另一個磁碟的 `ram01`。
//
// The disk's name plus the partition number. A name ending in a digit gets a p
// in between, so the first partition of `ram0` is not `ram01`, which reads like
// another disk.
bool partition_name(const char* disk, std::uint32_t number, char* name, std::size_t capacity) {
    char digits[12];
    if (format::to_decimal(digits, sizeof(digits), number) == 0) return false;
    if (!text::copy(name, capacity, disk)) return false;
    const auto length = text::length(disk);
    if (length != 0 && disk[length - 1] >= '0' && disk[length - 1] <= '9' &&
        !text::append(name, capacity, 'p'))
        return false;
    return text::append(name, capacity, digits);
}

} // namespace

std::size_t publish(device::Device& disk) {
    if (!disk.is_block_device()) return 0;
    device::BlockView view(&disk);
    Table table;
    if (!read(view, table)) return 0;

    std::size_t published = 0;
    for (std::size_t i = 0; i < table.count; ++i) {
        Slot* slot = nullptr;
        for (auto& candidate : slots) {
            if (!candidate.used) {
                slot = &candidate;
                break;
            }
        }
        if (slot == nullptr) break;
        char name[device::max_name_length + 1];
        if (!partition_name(disk.name, table.entries[i].number, name, sizeof(name))) continue;

        slot->disk_view.bind(&disk);
        slot->window.attach(slot->disk_view, table.entries[i].first, table.entries[i].count);
        slot->device = device::Device(name, device::Type::Block, device::block_operations, &slot->window);
        if (device::register_device(slot->device) != device::Status::Ok) continue;
        slot->disk = &disk;
        slot->used = true;
        ++published;
    }
    return published;
}

void unpublish(device::Device& disk) {
    for (auto& slot : slots) {
        if (!slot.used || slot.disk != &disk) continue;
        device::unregister_device(slot.device);
        slot = Slot{};
    }
}

} // namespace shirley::partition
