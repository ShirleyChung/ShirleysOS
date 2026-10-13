#include "shirley/exfat.hpp"

#include "shirley/heap.hpp"
#include "shirley/text.hpp"

#include <limits>

namespace shirley::exfat {
namespace {

// 磁碟上的一切都是小端序，而且逐位元組解碼，核心的結構排列方式因此不影響
// 能不能讀懂卷冊。
// Everything on disk is little-endian and decoded byte by byte, so the
// kernel's struct layout cannot change how a volume is read.
std::uint16_t le16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}
std::uint32_t le32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}
std::uint64_t le64(const std::uint8_t* bytes) {
    return le32(bytes) | (static_cast<std::uint64_t>(le32(bytes + 4)) << 32);
}

constexpr std::size_t entry_bytes = 32;
constexpr std::size_t name_units_per_entry = 15;
// File 項目之後最多 18 個次要項目：一個 Stream Extension 加上 17 個名稱項目。
// At most 18 secondary entries follow a File entry: one Stream Extension and
// seventeen name entries.
constexpr std::size_t max_set_entries = 19;
constexpr std::uint32_t first_data_cluster = 2;
constexpr std::uint32_t end_of_chain = 0xFFFFFFFF;
constexpr std::size_t max_block_bytes = 4096;
// exFAT 規格把目錄大小限制在 256 MiB，也就是 2^23 個項目；節點識別碼裡的
// 位置欄位因此 23 位元就夠。
//
// The specification caps a directory at 256 MiB, which is 2^23 entries, so 23
// bits are enough for the slot field of a node identifier.
constexpr std::uint32_t max_slot = (1u << 23) - 1;

// 目錄項目類型。最高位元表示使用中；被刪除的項目只清掉那一位。
// Directory entry types. The top bit means in use; deleting an entry clears
// only that bit.
constexpr std::uint8_t type_end = 0x00;
constexpr std::uint8_t type_bitmap = 0x81;
constexpr std::uint8_t type_upcase = 0x82;
constexpr std::uint8_t type_label = 0x83;
constexpr std::uint8_t type_file = 0x85;
constexpr std::uint8_t type_stream = 0xC0;
constexpr std::uint8_t type_name = 0xC1;
constexpr std::uint8_t secondary_flag_contiguous = 0x02;
constexpr std::uint16_t volume_flag_active_fat = 0x0001;
constexpr std::uint16_t volume_flag_dirty = 0x0002;

constexpr char signature[] = "EXFAT   ";

bool signature_matches(const std::uint8_t* sector) {
    for (std::size_t i = 0; i < 8; ++i)
        if (sector[3 + i] != static_cast<std::uint8_t>(signature[i])) return false;
    return true;
}

// 規格裡的三種校驗和都是同一個模式：右旋一位再加上下一個位元組，只是寬度
// 不同。
// All three checksums in the specification follow one pattern, rotate right by
// one and add the next byte, differing only in width.
std::uint32_t checksum32(std::uint32_t sum, std::uint8_t byte) {
    return ((sum & 1) ? 0x80000000u : 0) + (sum >> 1) + byte;
}
std::uint16_t checksum16(std::uint16_t sum, std::uint8_t byte) {
    return static_cast<std::uint16_t>(((sum & 1) ? 0x8000u : 0) + (sum >> 1) + byte);
}

// 節點識別碼：0 是根目錄；其他節點記錄它所在的父目錄（第一個叢集與是否連續）
// 以及那組項目在父目錄中的位置。檔案本身的叢集與長度不放進來，而是每次從
// 項目重新讀出，因此識別碼永遠不會和磁碟上的內容不一致。
//
//     bits 0-31   父目錄的第一個叢集
//     bits 32-54  項目在父目錄中的位置（32 位元組為單位）
//     bit  55     父目錄是否連續配置
//
// A node identifier: 0 is the root. Any other node records the parent
// directory holding it (first cluster, and whether it is contiguous) and where
// its entry set sits inside that parent. The file's own clusters and length are
// not packed in; they are read from the entries each time, so an identifier
// can never disagree with what is on disk.
//
//     bits 0-31   the parent directory's first cluster
//     bits 32-54  the entry set's slot in the parent, in 32-byte units
//     bit  55     whether the parent is allocated contiguously
constexpr std::uint64_t root_id = 0;
std::uint64_t encode_id(const Volume::Stream& parent, std::uint32_t slot) {
    return parent.first_cluster | (static_cast<std::uint64_t>(slot) << 32) |
           (static_cast<std::uint64_t>(parent.contiguous) << 55);
}

} // namespace

const char* mount_error_text(MountError error) {
    switch (error) {
    case MountError::None: return "mounted";
    case MountError::NotExfat: return "not an exFAT volume";
    case MountError::Unsupported: return "unsupported exFAT variant";
    case MountError::Corrupt: return "corrupt exFAT volume";
    case MountError::DeviceError: return "device error";
    case MountError::OutOfMemory: return "out of memory";
    case MountError::AlreadyMounted: return "already mounted";
    }
    return "unknown error";
}

bool identify(io::BlockDevice& device) {
    const auto block = device.block_size();
    if (block < 512 || block > max_block_bytes) return false;
    std::uint8_t sector[max_block_bytes];
    if (!device.read_blocks(0, 1, sector)) return false;
    return signature_matches(sector) && sector[510] == 0x55 && sector[511] == 0xAA;
}

MountError Volume::mount(io::BlockDevice& device) {
    if (mounted()) return MountError::AlreadyMounted;
    const auto block = device.block_size();
    if (block < 512 || block > max_block_bytes || (block & (block - 1)) != 0)
        return MountError::Unsupported;
    bounce_ = static_cast<std::uint8_t*>(heap::allocate(block));
    if (bounce_ == nullptr) return MountError::OutOfMemory;
    device_ = &device;
    // 失敗時一律走這裡，把已經配置的東西還回去，維持未掛載。
    // Every failure goes through here, returning whatever was allocated and
    // staying unmounted.
    const auto fail = [this](MountError error) {
        unmount();
        return error;
    };

    std::uint8_t boot[512];
    if (!read_bytes(0, boot, sizeof(boot))) return fail(MountError::DeviceError);
    if (!signature_matches(boot) || boot[510] != 0x55 || boot[511] != 0xAA)
        return fail(MountError::NotExfat);
    // 偏移 11 到 63 在 FAT 的 BPB 位置，exFAT 規定全為零，這樣舊的 FAT 驅動程式
    // 才不會把它誤認成 FAT。
    //
    // Offsets 11 through 63 are where FAT keeps its BPB, and exFAT requires them
    // all zero so an old FAT driver never mistakes the volume for FAT.
    for (std::size_t i = 11; i < 64; ++i)
        if (boot[i] != 0) return fail(MountError::Corrupt);

    const auto volume_sectors = le64(boot + 72);
    const auto fat_sector = le32(boot + 80);
    const auto fat_sectors = le32(boot + 84);
    const auto heap_sector = le32(boot + 88);
    const auto clusters = le32(boot + 92);
    const auto root_cluster = le32(boot + 96);
    const auto revision_major = boot[105];
    const auto flags = le16(boot + 106);
    const auto sector_shift = boot[108];
    const auto cluster_sector_shift = boot[109];
    const auto fat_count = boot[110];

    if (revision_major != 1) return fail(MountError::Unsupported);
    if (sector_shift < 9 || sector_shift > 12 || cluster_sector_shift > 25 - sector_shift ||
        (fat_count != 1 && fat_count != 2))
        return fail(MountError::Corrupt);
    const std::uint64_t sector_bytes = std::uint64_t{1} << sector_shift;
    // 叢集數上限讓 FAT 項目與叢集編號都留在 32 位元內。
    // The cluster count cap keeps both FAT entries and cluster numbers within
    // 32 bits.
    if (clusters == 0 || clusters > 0xFFFFFFF5u || root_cluster < first_data_cluster ||
        root_cluster > clusters + 1 || fat_sector < 24 ||
        static_cast<std::uint64_t>(fat_sectors) * sector_bytes <
            (static_cast<std::uint64_t>(clusters) + 2) * 4 ||
        heap_sector < static_cast<std::uint64_t>(fat_sector) +
                          static_cast<std::uint64_t>(fat_sectors) * fat_count ||
        heap_sector + (static_cast<std::uint64_t>(clusters) << cluster_sector_shift) >
            volume_sectors)
        return fail(MountError::Corrupt);
    const auto device_bytes = device.block_count() * block;
    if (volume_sectors > device_bytes / sector_bytes) return fail(MountError::Corrupt);

    // 開機校驗和：前 11 個磁區（略過會變動的 VolumeFlags 與 PercentInUse），
    // 第 12 個磁區整個填滿這個值。
    //
    // The boot checksum covers the first 11 sectors, skipping VolumeFlags and
    // PercentInUse because those change; the twelfth sector is filled with it.
    std::uint32_t sum = 0;
    std::uint8_t chunk[512];
    for (std::uint64_t offset = 0; offset < 11 * sector_bytes; offset += sizeof(chunk)) {
        if (!read_bytes(offset, chunk, sizeof(chunk))) return fail(MountError::DeviceError);
        for (std::size_t i = 0; i < sizeof(chunk); ++i) {
            const auto at = offset + i;
            if (at == 106 || at == 107 || at == 112) continue;
            sum = checksum32(sum, chunk[i]);
        }
    }
    for (std::uint64_t offset = 11 * sector_bytes; offset < 12 * sector_bytes;
         offset += sizeof(chunk)) {
        if (!read_bytes(offset, chunk, sizeof(chunk))) return fail(MountError::DeviceError);
        for (std::size_t i = 0; i < sizeof(chunk); i += 4)
            if (le32(chunk + i) != sum) return fail(MountError::Corrupt);
    }

    cluster_count_ = clusters;
    cluster_shift_ = sector_shift + cluster_sector_shift;
    fat_offset_ = fat_sector * sector_bytes;
    // 有兩份 FAT 時，ActiveFat 指出現在用的是哪一份。
    // With two FATs, ActiveFat says which one is in use.
    if (fat_count == 2 && (flags & volume_flag_active_fat) != 0)
        fat_offset_ += fat_sectors * sector_bytes;
    heap_offset_ = heap_sector * sector_bytes;
    serial_ = le32(boot + 100);
    dirty_ = (flags & volume_flag_dirty) != 0;

    // 根目錄沒有自己的 Stream Extension，長度要沿著 FAT 走一遍才知道。
    // The root directory has no Stream Extension of its own; its length is only
    // known by walking the FAT.
    std::uint64_t root_clusters = 1;
    for (std::uint32_t cluster = root_cluster;; ++root_clusters) {
        std::uint32_t next = 0;
        if (!fat_next(cluster, next)) return fail(MountError::Corrupt);
        if (next == end_of_chain) break;
        if (next < first_data_cluster || next > clusters + 1 || root_clusters >= clusters)
            return fail(MountError::Corrupt);
        cluster = next;
    }
    root_ = Stream{root_cluster, root_clusters << cluster_shift_, root_clusters << cluster_shift_,
                   false};

    // 根目錄裡的必要項目：配置位元圖與大寫表；卷冊標籤可有可無。
    // The root directory's required entries are the allocation bitmap and the
    // up-case table; a volume label is optional.
    bool bitmap = false;
    bool upcase = false;
    Stream upcase_table{};
    std::uint32_t upcase_checksum = 0;
    for (std::uint64_t offset = 0; offset < root_.length; offset += entry_bytes) {
        std::uint8_t entry[entry_bytes];
        if (!read_stream(root_, offset, entry, sizeof(entry))) return fail(MountError::Corrupt);
        if (entry[0] == type_end) break;
        if (entry[0] == type_bitmap) {
            bitmap = true;
        } else if (entry[0] == type_upcase && !upcase) {
            upcase = true;
            upcase_checksum = le32(entry + 4);
            upcase_table.first_cluster = le32(entry + 20);
            upcase_table.length = upcase_table.valid_length = le64(entry + 24);
        } else if (entry[0] == type_label) {
            const std::size_t count = entry[1] < 11 ? entry[1] : 11;
            char16_t units[11];
            for (std::size_t i = 0; i < count; ++i) units[i] = le16(entry + 2 + i * 2);
            if (!text::utf16_to_utf8(units, count, label_, sizeof(label_))) label_[0] = '\0';
        }
    }
    if (!bitmap || !upcase) return fail(MountError::Corrupt);
    upcase_ = static_cast<char16_t*>(heap::allocate(65536 * sizeof(char16_t)));
    if (upcase_ == nullptr) return fail(MountError::OutOfMemory);
    if (!load_upcase(upcase_table, upcase_checksum)) return fail(MountError::Corrupt);
    return MountError::None;
}

void Volume::unmount() {
    heap::free(bounce_);
    heap::free(upcase_);
    *this = Volume{};
}

// 大寫表可以壓縮：0xFFFF 後面跟著一個數字 n，表示接下來 n 個字元對應到自己。
// 表沒有涵蓋到的字元同樣對應到自己。
//
// The up-case table may be compressed: 0xFFFF followed by a count n means the
// next n characters map to themselves. Characters past the end of the table
// map to themselves as well.
bool Volume::load_upcase(const Stream& table, std::uint32_t checksum) {
    if (table.length == 0 || table.length > 2 * 65536 * 2 || (table.length & 1) != 0 ||
        table.first_cluster < first_data_cluster || table.first_cluster > cluster_count_ + 1)
        return false;
    for (std::size_t i = 0; i < 65536; ++i) upcase_[i] = static_cast<char16_t>(i);
    std::uint32_t sum = 0;
    std::size_t next_unit = 0;
    bool run_follows = false;
    std::uint8_t chunk[512];
    for (std::uint64_t offset = 0; offset < table.length; offset += sizeof(chunk)) {
        const auto bytes = table.length - offset < sizeof(chunk)
                               ? static_cast<std::size_t>(table.length - offset)
                               : sizeof(chunk);
        if (!read_stream(table, offset, chunk, bytes)) return false;
        for (std::size_t i = 0; i < bytes; ++i) sum = checksum32(sum, chunk[i]);
        for (std::size_t i = 0; i + 1 < bytes; i += 2) {
            const auto value = le16(chunk + i);
            if (run_follows) {
                next_unit += value;
                run_follows = false;
            } else if (value == 0xFFFF) {
                run_follows = true;
            } else if (next_unit < 65536) {
                upcase_[next_unit++] = static_cast<char16_t>(value);
            }
        }
    }
    return sum == checksum;
}

char16_t Volume::upcase(char16_t unit) const { return upcase_[unit]; }

io::Result Volume::read_bytes(std::uint64_t offset, void* buffer, std::size_t length) {
    const auto block = device_->block_size();
    auto* out = static_cast<std::uint8_t*>(buffer);
    std::size_t done = 0;
    while (done < length) {
        const auto at = offset + done;
        const auto index = at / block;
        const auto within = static_cast<std::size_t>(at % block);
        const auto remaining = length - done;
        // 對齊而且夠長的部分直接讀進呼叫端的緩衝區，其餘經過跳板。
        // An aligned, long enough stretch goes straight into the caller's
        // buffer; anything else goes through the bounce buffer.
        if (within == 0 && remaining >= block) {
            const auto count = remaining / block;
            const auto result = device_->read_blocks(index, count, out + done);
            if (!result) return {done, result.error};
            done += count * block;
            continue;
        }
        const auto result = device_->read_blocks(index, 1, bounce_);
        if (!result) return {done, result.error};
        const auto take = block - within < remaining ? block - within : remaining;
        for (std::size_t i = 0; i < take; ++i) out[done + i] = bounce_[within + i];
        done += take;
    }
    return {done, io::Error::None};
}

bool Volume::fat_next(std::uint32_t cluster, std::uint32_t& next) {
    if (cluster < first_data_cluster || cluster > cluster_count_ + 1) return false;
    std::uint8_t value[4];
    if (!read_bytes(fat_offset_ + static_cast<std::uint64_t>(cluster) * 4, value, 4)) return false;
    next = le32(value);
    return true;
}

bool Volume::cluster_at(const Stream& stream, std::uint64_t index, std::uint32_t& cluster) {
    const auto last = static_cast<std::uint64_t>(cluster_count_) + 1;
    if (stream.first_cluster < first_data_cluster || stream.first_cluster > last ||
        index >= cluster_count_)
        return false;
    if (stream.contiguous) {
        if (stream.first_cluster + index > last) return false;
        cluster = static_cast<std::uint32_t>(stream.first_cluster + index);
        return true;
    }
    std::uint32_t current = stream.first_cluster;
    std::uint64_t at = 0;
    if (cursor_.first_cluster == stream.first_cluster && cursor_.index <= index) {
        current = cursor_.cluster;
        at = cursor_.index;
    }
    // index 小於叢集總數，因此即使 FAT 串成一個環，這個迴圈也一定會結束。
    // index is below the cluster count, so this loop ends even when the FAT
    // has been chained into a loop.
    for (; at < index; ++at) {
        std::uint32_t next = 0;
        if (!fat_next(current, next) || next < first_data_cluster || next > last) return false;
        current = next;
    }
    cursor_.first_cluster = stream.first_cluster;
    cursor_.index = index;
    cursor_.cluster = current;
    cluster = current;
    return true;
}

io::Result Volume::read_stream(const Stream& stream, std::uint64_t offset, void* buffer,
                               std::size_t length) {
    if (offset >= stream.length) return {0, io::Error::None};
    if (length > stream.length - offset) length = static_cast<std::size_t>(stream.length - offset);
    const auto cluster_bytes = std::uint64_t{1} << cluster_shift_;
    auto* out = static_cast<std::uint8_t*>(buffer);
    std::size_t done = 0;
    while (done < length) {
        const auto at = offset + done;
        const auto within = at & (cluster_bytes - 1);
        auto take = cluster_bytes - within;
        if (take > length - done) take = length - done;
        std::uint32_t cluster = 0;
        if (!cluster_at(stream, at >> cluster_shift_, cluster)) return {done, io::Error::DeviceError};
        const auto position =
            heap_offset_ + (static_cast<std::uint64_t>(cluster - first_data_cluster) << cluster_shift_) +
            within;
        const auto result = read_bytes(position, out + done, static_cast<std::size_t>(take));
        if (!result) return {done, result.error};
        done += static_cast<std::size_t>(take);
    }
    return {done, io::Error::None};
}

// 從 slot 開始找下一組完整而且校驗和正確的 File 項目組，找到時把 slot 推到
// 這組之後。被刪除的項目、其他種類的主要項目，以及壞掉的項目組都被跳過：
// 一組壞掉的項目不應該讓整個目錄都讀不了。
//
// Find the next complete File entry set with a correct checksum, starting at
// slot, and move slot past it when one is found. Deleted entries, other kinds
// of primary entry and damaged sets are all skipped: one damaged set should
// not make the whole directory unreadable.
bool Volume::next_entry(const Stream& directory, std::uint32_t& slot, Entry& entry) {
    std::uint8_t set[max_set_entries * entry_bytes];
    while (slot <= max_slot && static_cast<std::uint64_t>(slot) * entry_bytes < directory.length) {
        const auto offset = static_cast<std::uint64_t>(slot) * entry_bytes;
        if (read_stream(directory, offset, set, entry_bytes).transferred != entry_bytes) return false;
        if (set[0] == type_end) return false;
        if (set[0] != type_file) {
            ++slot;
            continue;
        }
        const std::size_t secondaries = set[1];
        const auto set_bytes = (secondaries + 1) * entry_bytes;
        if (secondaries < 2 || secondaries > max_set_entries - 1 ||
            read_stream(directory, offset, set, set_bytes).transferred != set_bytes) {
            ++slot;
            continue;
        }
        std::uint16_t sum = 0;
        for (std::size_t i = 0; i < set_bytes; ++i)
            if (i != 2 && i != 3) sum = checksum16(sum, set[i]);
        const auto* stream = set + entry_bytes;
        const std::size_t name_length = stream[3];
        const auto name_entries = (name_length + name_units_per_entry - 1) / name_units_per_entry;
        bool valid = sum == le16(set + 2) && stream[0] == type_stream && name_length != 0 &&
                     name_entries <= secondaries - 1;
        for (std::size_t i = 0; valid && i < name_entries; ++i)
            valid = set[(2 + i) * entry_bytes] == type_name;
        // 其餘的次要項目（廠商延伸）不認得也沒關係，但必須是使用中的次要項目。
        // The remaining secondaries (vendor extensions) need not be understood,
        // but they must be in-use secondary entries.
        for (std::size_t i = 2 + name_entries; valid && i <= secondaries; ++i)
            valid = (set[i * entry_bytes] & 0xC0) == 0xC0;
        if (!valid) {
            ++slot;
            continue;
        }

        entry.attributes = le16(set + 4);
        entry.name_length = static_cast<std::uint8_t>(name_length);
        entry.name_hash = le16(stream + 4);
        entry.stream.contiguous = (stream[1] & secondary_flag_contiguous) != 0;
        entry.stream.valid_length = le64(stream + 8);
        entry.stream.first_cluster = le32(stream + 20);
        entry.stream.length = le64(stream + 24);
        for (std::size_t i = 0; i < name_length; ++i) {
            const auto* unit = set + (2 + i / name_units_per_entry) * entry_bytes + 2 +
                               (i % name_units_per_entry) * 2;
            entry.name[i] = le16(unit);
        }
        entry.slot = slot;
        slot += static_cast<std::uint32_t>(secondaries + 1);
        if (!valid_name(entry)) continue;

        // 長度與叢集必須說得通，否則讀取會跑到卷冊外面去。
        // Length and clusters have to make sense, or reading would wander off
        // the volume.
        const auto heap_bytes = static_cast<std::uint64_t>(cluster_count_) << cluster_shift_;
        const auto& content = entry.stream;
        if (content.valid_length > content.length || content.length > heap_bytes) continue;
        if (content.length != 0 &&
            (content.first_cluster < first_data_cluster || content.first_cluster > cluster_count_ + 1))
            continue;
        if (content.contiguous && content.length != 0) {
            const auto span = (content.length + (std::uint64_t{1} << cluster_shift_) - 1) >> cluster_shift_;
            if (content.first_cluster - first_data_cluster + span > cluster_count_) continue;
        }
        return true;
    }
    return false;
}

// 名稱必須合法，而且與 Stream Extension 裡的雜湊一致。不合法的名稱不只是格式
// 問題：含有 '/' 的名稱會被當成路徑拆開，叫做 "." 或 ".." 的項目會讓走訪目錄樹
// 繞回自己。雜湊不符的項目則會列得出來卻查不到，因為 lookup() 先比雜湊。
//
// A name has to be legal and agree with the hash in the Stream Extension. An
// illegal name is more than a formatting problem: one containing '/' would be
// split as a path, and an entry called "." or ".." would send a tree walk back
// into itself. An entry whose hash disagrees would list but never look up,
// since lookup() compares the hash first.
bool Volume::valid_name(const Entry& entry) const {
    const auto length = entry.name_length;
    if ((length == 1 && entry.name[0] == u'.') ||
        (length == 2 && entry.name[0] == u'.' && entry.name[1] == u'.'))
        return false;
    std::uint16_t hash = 0;
    for (std::size_t i = 0; i < length; ++i) {
        const auto unit = entry.name[i];
        if (unit < 0x20 || unit == u'"' || unit == u'*' || unit == u'/' || unit == u':' ||
            unit == u'<' || unit == u'>' || unit == u'?' || unit == u'\\' || unit == u'|')
            return false;
        const auto upper = upcase(unit);
        hash = checksum16(hash, static_cast<std::uint8_t>(upper & 0xFF));
        hash = checksum16(hash, static_cast<std::uint8_t>(upper >> 8));
    }
    return hash == entry.name_hash;
}

bool Volume::decode(const vfs::Node& node, Stream& stream, bool& directory) {
    if (!mounted() || node.filesystem != this) return false;
    if (node.id == root_id) {
        stream = root_;
        directory = true;
        return true;
    }
    Stream parent{};
    parent.first_cluster = static_cast<std::uint32_t>(node.id);
    parent.contiguous = ((node.id >> 55) & 1) != 0;
    const auto slot = static_cast<std::uint32_t>((node.id >> 32) & max_slot);
    // 父目錄的長度不在識別碼裡；剛好讀得到這一組項目就夠了。
    // The parent's length is not in the identifier; reaching exactly this one
    // set is all that is needed.
    parent.length = parent.valid_length = (static_cast<std::uint64_t>(slot) + max_set_entries) * entry_bytes;
    std::uint32_t cursor = slot;
    Entry entry;
    if (!next_entry(parent, cursor, entry) || entry.slot != slot) return false;
    stream = entry.stream;
    directory = entry.directory();
    return true;
}

bool Volume::to_node(const Stream& parent, const Entry& entry, vfs::Node& node) {
    if (entry.slot > max_slot) return false;
    node = vfs::Node{};
    node.filesystem = this;
    node.id = encode_id(parent, entry.slot);
    node.type = entry.directory() ? vfs::Type::Directory : vfs::Type::File;
    node.size = entry.stream.length;
    return text::utf16_to_utf8(entry.name, entry.name_length, node.name, sizeof(node.name));
}

bool Volume::root(vfs::Node& node) {
    if (!mounted()) return false;
    node = vfs::Node{};
    node.filesystem = this;
    node.id = root_id;
    node.type = vfs::Type::Directory;
    node.size = root_.length;
    return true;
}

// 名稱不分大小寫：兩邊都經過卷冊自己的大寫表再比較。Stream Extension 裡存著
// 大寫名稱的雜湊，先比雜湊就能跳過幾乎所有不相符的項目。
//
// Names compare case-insensitively: both sides go through the volume's own
// up-case table first. The Stream Extension stores a hash of the up-cased name,
// and comparing that first skips nearly every entry that does not match.
bool Volume::lookup(const vfs::Node& directory, const char* name, vfs::Node& result) {
    Stream stream{};
    bool is_directory = false;
    if (!decode(directory, stream, is_directory) || !is_directory) return false;
    char16_t wanted[255];
    std::size_t length = 0;
    if (!text::utf8_to_utf16(name, wanted, 255, length) || length == 0) return false;
    std::uint16_t hash = 0;
    for (std::size_t i = 0; i < length; ++i) {
        wanted[i] = upcase(wanted[i]);
        hash = checksum16(hash, static_cast<std::uint8_t>(wanted[i] & 0xFF));
        hash = checksum16(hash, static_cast<std::uint8_t>(wanted[i] >> 8));
    }

    Entry entry;
    for (std::uint32_t slot = 0; next_entry(stream, slot, entry);) {
        if (entry.name_length != length || entry.name_hash != hash) continue;
        bool same = true;
        for (std::size_t i = 0; same && i < length; ++i) same = upcase(entry.name[i]) == wanted[i];
        if (same) return to_node(stream, entry, result);
    }
    return false;
}

bool Volume::list(const vfs::Node& directory, std::size_t position, vfs::Node& child) {
    Stream stream{};
    bool is_directory = false;
    if (!decode(directory, stream, is_directory) || !is_directory) return false;
    Entry entry;
    std::size_t index = 0;
    for (std::uint32_t slot = 0; next_entry(stream, slot, entry);) {
        // 換成 UTF-8 之後超過名稱上限的項目查不到，所以也不列出來；兩邊一致
        // 比列出一個打不開的名字好。
        //
        // An entry whose name exceeds the limit once in UTF-8 cannot be looked
        // up, so it is not listed either; agreeing with lookup beats listing a
        // name that cannot be opened.
        if (!to_node(stream, entry, child)) continue;
        if (index++ == position) return true;
    }
    return false;
}

io::Result Volume::read(const vfs::Node& file, std::uint64_t offset, void* buffer,
                        std::size_t length) {
    Stream stream{};
    bool is_directory = false;
    if (!decode(file, stream, is_directory) || is_directory) return {0, io::Error::InvalidArgument};
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (offset >= stream.length) return {0, io::Error::None};
    if (length > stream.length - offset) length = static_cast<std::size_t>(stream.length - offset);
    // ValidDataLength 之後的位元組在磁碟上沒有意義，規格要求讀成零。
    // Bytes past ValidDataLength mean nothing on disk, and the specification
    // has them read as zero.
    std::size_t stored = 0;
    if (offset < stream.valid_length) {
        stored = stream.valid_length - offset < length
                     ? static_cast<std::size_t>(stream.valid_length - offset)
                     : length;
        const auto result = read_stream(stream, offset, buffer, stored);
        if (!result) return result;
    }
    auto* out = static_cast<std::uint8_t*>(buffer);
    for (std::size_t i = stored; i < length; ++i) out[i] = 0;
    return {length, io::Error::None};
}

io::Result Volume::write(const vfs::Node&, std::uint64_t, const void*, std::size_t) {
    return {0, io::Error::Unsupported};
}

bool Volume::writable(const vfs::Node&) const { return false; }

} // namespace shirley::exfat
