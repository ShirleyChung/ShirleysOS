#include "shirley/ext4.hpp"

#include "shirley/checksum.hpp"
#include "shirley/heap.hpp"
#include "shirley/text.hpp"

namespace shirley::ext4 {
namespace {

// 磁碟上的一切都是小端序，逐位元組解碼。
// Everything on disk is little-endian and decoded byte by byte.
std::uint16_t le16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
}
std::uint32_t le32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

constexpr std::uint64_t superblock_offset = 1024;
constexpr std::size_t superblock_bytes = 1024;
constexpr std::uint16_t magic = 0xEF53;
constexpr std::size_t max_device_block = 4096;
constexpr std::uint32_t max_block_shift = 16;
constexpr std::uint32_t root_inode = 2;
// 一個 inode 裡會讀到的部分：i_block 與 i_size_high 都在前 128 位元組。
// The part of an inode this driver reads: i_block and i_size_high both sit in
// the first 128 bytes.
constexpr std::size_t inode_core_bytes = 128;

// 功能旗標。 / Feature flags.
constexpr std::uint32_t compat_has_journal = 0x0004;
constexpr std::uint32_t compat_sparse_super2 = 0x0200;
constexpr std::uint32_t incompat_compression = 0x0001;
constexpr std::uint32_t incompat_filetype = 0x0002;
constexpr std::uint32_t incompat_recover = 0x0004;
constexpr std::uint32_t incompat_meta_bg = 0x0010;
constexpr std::uint32_t incompat_extents = 0x0040;
constexpr std::uint32_t incompat_64bit = 0x0080;
constexpr std::uint32_t incompat_mmp = 0x0100;
constexpr std::uint32_t incompat_flex_bg = 0x0200;
constexpr std::uint32_t incompat_ea_inode = 0x0400;
constexpr std::uint32_t incompat_csum_seed = 0x2000;
constexpr std::uint32_t incompat_largedir = 0x4000;
constexpr std::uint32_t incompat_encrypt = 0x10000;
constexpr std::uint32_t incompat_casefold = 0x20000;
constexpr std::uint32_t ro_compat_sparse_super = 0x0001;
constexpr std::uint32_t ro_compat_huge_file = 0x0008;
constexpr std::uint32_t ro_compat_gdt_csum = 0x0010;
constexpr std::uint32_t ro_compat_dir_nlink = 0x0020;
constexpr std::uint32_t ro_compat_extra_isize = 0x0040;
constexpr std::uint32_t ro_compat_bigalloc = 0x0200;
constexpr std::uint32_t ro_compat_metadata_csum = 0x0400;

// 唯讀時看得懂的 incompat 功能。其餘的——壓縮、外部 journal、dirdata、
// inline_data——都會讓掛載失敗。recover 另外處理。
//
// The incompat features a read-only mount understands. Anything else —
// compression, an external journal, dirdata, inline_data — fails the mount.
// recover is handled on its own.
constexpr std::uint32_t incompat_supported =
    incompat_filetype | incompat_recover | incompat_meta_bg | incompat_extents | incompat_64bit |
    incompat_mmp | incompat_flex_bg | incompat_ea_inode | incompat_csum_seed | incompat_largedir |
    incompat_encrypt | incompat_casefold;
// 只有 ext4 才有的功能；有任何一個就叫 ext4，否則有 journal 叫 ext3，都沒有叫
// ext2。
// Features only ext4 has; any of them makes the volume ext4, otherwise a
// journal makes it ext3, and neither makes it ext2.
constexpr std::uint32_t incompat_ext4_only =
    incompat_extents | incompat_64bit | incompat_flex_bg | incompat_mmp | incompat_ea_inode |
    incompat_csum_seed | incompat_largedir | incompat_encrypt | incompat_casefold;
constexpr std::uint32_t ro_compat_ext4_only = ro_compat_huge_file | ro_compat_gdt_csum |
                                              ro_compat_dir_nlink | ro_compat_extra_isize |
                                              ro_compat_metadata_csum;

// inode 旗標。 / Inode flags.
constexpr std::uint32_t inode_flag_encrypt = 0x00000800;
constexpr std::uint32_t inode_flag_huge_file = 0x00040000;
constexpr std::uint32_t inode_flag_extents = 0x00080000;
constexpr std::uint32_t inode_flag_inline_data = 0x10000000;

constexpr std::uint16_t extent_magic = 0xF30A;
constexpr std::size_t extent_header_bytes = 12;
constexpr std::size_t extent_entry_bytes = 12;
// Linux 的 extent 樹最深 5 層；更深的一定是壞掉的。
// Linux's extent trees are at most 5 deep; anything deeper is damage.
constexpr std::uint16_t max_extent_depth = 5;
// 長度超過這個值的 extent 是「未初始化」的：已經配置但還沒寫過，讀起來是零。
// An extent longer than this is uninitialized: allocated but never written,
// and it reads as zero.
constexpr std::uint16_t max_initialized_extent = 32768;
// 邏輯區塊編號是 32 位元。 / Logical block numbers are 32 bits.
constexpr std::uint64_t logical_limit = 1ull << 32;

constexpr std::size_t direct_blocks = 12;
constexpr std::size_t symlink_inline_bytes = 60;

// 規格把 1、3、5、7 的冪次群組當成備份超級區塊的位置。
// sparse_super puts the backup superblocks in groups that are powers of 3, 5
// and 7.
bool power_of(std::uint32_t value, std::uint32_t base) {
    std::uint64_t power = base;
    while (power < value) power *= base;
    return power == value;
}

} // namespace

const char* mount_error_text(MountError error) {
    switch (error) {
    case MountError::None: return "mounted";
    case MountError::NotExt4: return "not an ext2/3/4 volume";
    case MountError::Unsupported: return "unsupported ext4 feature";
    case MountError::NeedsRecovery: return "the journal needs recovery (run e2fsck)";
    case MountError::Corrupt: return "corrupt ext4 volume";
    case MountError::DeviceError: return "device error";
    case MountError::OutOfMemory: return "out of memory";
    case MountError::AlreadyMounted: return "already mounted";
    }
    return "unknown error";
}

bool identify(io::BlockDevice& device) {
    const auto block = device.block_size();
    if (block < 512 || block > max_device_block || (block & (block - 1)) != 0) return false;
    // 魔數在位元組 1080，無論裝置區塊是 512 還是 4096 都在某一個區塊裡。
    // The magic number is at byte 1080, which falls inside one device block
    // whether that block is 512 bytes or 4096.
    constexpr std::uint64_t at = superblock_offset + 56;
    std::uint8_t sector[max_device_block];
    if (device.block_count() <= at / block) return false;
    if (!device.read_blocks(at / block, 1, sector)) return false;
    return le16(sector + at % block) == magic;
}

MountError Volume::mount(io::BlockDevice& device) {
    if (mounted()) return MountError::AlreadyMounted;
    const auto block = device.block_size();
    if (block < 512 || block > max_device_block || (block & (block - 1)) != 0)
        return MountError::Unsupported;
    if (device.block_count() * block < superblock_offset + superblock_bytes)
        return MountError::NotExt4;
    bounce_ = static_cast<std::uint8_t*>(heap::allocate(block));
    if (bounce_ == nullptr) return MountError::OutOfMemory;
    device_ = &device;
    const auto fail = [this](MountError error) {
        release();
        return error;
    };

    std::uint8_t sb[superblock_bytes];
    if (!read_bytes(superblock_offset, sb, sizeof(sb))) return fail(MountError::DeviceError);
    if (le16(sb + 56) != magic) return fail(MountError::NotExt4);

    const auto log_block = le32(sb + 24);
    if (log_block > max_block_shift - 10) return fail(MountError::Unsupported);
    block_shift_ = 10 + log_block;
    block_bytes_ = 1u << block_shift_;

    // 修訂版 0 是最早的 ext2：固定 128 位元組的 inode，沒有功能旗標。
    // Revision 0 is the original ext2: fixed 128-byte inodes and no feature
    // flags.
    const auto revision = le32(sb + 76);
    if (revision > 1) return fail(MountError::Unsupported);
    inode_bytes_ = revision == 0 ? 128 : le16(sb + 88);
    if (revision == 1) {
        compatible_ = le32(sb + 92);
        incompatible_ = le32(sb + 96);
        read_only_compatible_ = le32(sb + 100);
    }
    if (inode_bytes_ < inode_core_bytes || inode_bytes_ > block_bytes_ ||
        (inode_bytes_ & (inode_bytes_ - 1)) != 0)
        return fail(MountError::Corrupt);

    if ((incompatible_ & ~incompat_supported) != 0 ||
        (read_only_compatible_ & ro_compat_bigalloc) != 0)
        return fail(MountError::Unsupported);
    if ((incompatible_ & incompat_recover) != 0) return fail(MountError::NeedsRecovery);
    // metadata_csum：超級區塊最後四個位元組是前面 1020 個位元組的 CRC-32C。
    // metadata_csum: the superblock's last four bytes are the CRC-32C of the
    // 1020 before them.
    if ((read_only_compatible_ & ro_compat_metadata_csum) != 0) {
        if (sb[373] != 1) return fail(MountError::Unsupported);
        if (~checksum::crc32c(sb, superblock_bytes - 4) != le32(sb + superblock_bytes - 4))
            return fail(MountError::Corrupt);
    }

    const bool wide = (incompatible_ & incompat_64bit) != 0;
    inodes_count_ = le32(sb + 0);
    blocks_count_ = le32(sb + 4) | (wide ? static_cast<std::uint64_t>(le32(sb + 336)) << 32 : 0);
    first_data_block_ = le32(sb + 20);
    blocks_per_group_ = le32(sb + 32);
    inodes_per_group_ = le32(sb + 40);
    descriptor_bytes_ = wide ? le16(sb + 254) : 32;
    if ((incompatible_ & incompat_meta_bg) != 0) first_meta_bg_ = le32(sb + 260);
    backup_groups_[0] = le32(sb + 588);
    backup_groups_[1] = le32(sb + 592);

    const auto bits_per_block = 8ull * block_bytes_;
    if (blocks_per_group_ < 8 || blocks_per_group_ > bits_per_block || inodes_per_group_ == 0 ||
        inodes_per_group_ > bits_per_block || first_data_block_ > 1 ||
        first_data_block_ >= blocks_count_ || blocks_count_ >= (1ull << 48))
        return fail(MountError::Corrupt);
    if (wide && (descriptor_bytes_ < 64 || descriptor_bytes_ > block_bytes_ ||
                 (descriptor_bytes_ & (descriptor_bytes_ - 1)) != 0))
        return fail(MountError::Corrupt);
    const auto groups = (blocks_count_ - first_data_block_ + blocks_per_group_ - 1) / blocks_per_group_;
    if (groups == 0 || groups * inodes_per_group_ != inodes_count_)
        return fail(MountError::Corrupt);
    group_count_ = static_cast<std::uint32_t>(groups);
    // 卷冊比裝置大，代表映像被截斷或分割區切錯了。
    // A volume larger than its device means a truncated image or a partition
    // cut in the wrong place.
    if (blocks_count_ > (device.block_count() * block) >> block_shift_)
        return fail(MountError::Corrupt);

    // s_state：位元 0 是乾淨卸載，位元 1 是發現過錯誤。
    // s_state: bit 0 means cleanly unmounted, bit 1 means errors were found.
    const auto state = le16(sb + 58);
    dirty_ = (state & 1) == 0 || (state & 2) != 0;
    for (std::size_t i = 0; i < 16 && sb[120 + i] != 0; ++i) label_[i] = static_cast<char>(sb[120 + i]);
    if ((incompatible_ & incompat_ext4_only) != 0 ||
        (read_only_compatible_ & ro_compat_ext4_only) != 0)
        name_ = "ext4 (ro)";
    else if ((compatible_ & compat_has_journal) != 0)
        name_ = "ext3 (ro)";
    else
        name_ = "ext2 (ro)";

    block_ = static_cast<std::uint8_t*>(heap::allocate(block_bytes_));
    if (block_ == nullptr) return fail(MountError::OutOfMemory);

    Inode root;
    if (!load_inode(root_inode, root) || !root.directory()) return fail(MountError::Corrupt);
    return MountError::None;
}

void Volume::unmount() {
    if (mounted()) release();
}

void Volume::release() {
    heap::free(bounce_);
    heap::free(block_);
    *this = Volume{};
}

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

bool Volume::has_superblock(std::uint32_t group) const {
    if (group == 0) return true;
    if ((compatible_ & compat_sparse_super2) != 0)
        return group == backup_groups_[0] || group == backup_groups_[1];
    if (group == 1 || (read_only_compatible_ & ro_compat_sparse_super) == 0) return true;
    if ((group & 1) == 0) return false;
    return power_of(group, 3) || power_of(group, 5) || power_of(group, 7);
}

// 群組描述子表通常緊接在超級區塊之後，整張表放在一起。meta_bg 卷冊從第
// first_meta_bg 個描述子區塊開始，改成每個 meta 群組（一個描述子區塊能描述的
// 那些群組）把自己的描述子區塊放在第一個群組的開頭，超級區塊備份之後。
//
// The group descriptor table usually follows the superblock in one piece. On a
// meta_bg volume, from descriptor block first_meta_bg on, each meta group —
// the groups one descriptor block describes — keeps its descriptor block at
// the start of its first group instead, after any backup superblock.
bool Volume::group_descriptor_offset(std::uint32_t group, std::uint64_t& offset) const {
    if (group >= group_count_) return false;
    const auto per_block = block_bytes_ / descriptor_bytes_;
    const auto index = group / per_block;
    std::uint64_t block = 0;
    if ((incompatible_ & incompat_meta_bg) != 0 && index >= first_meta_bg_) {
        const auto first = index * per_block;
        block = first_data_block_ + static_cast<std::uint64_t>(first) * blocks_per_group_ +
                (has_superblock(first) ? 1 : 0);
    } else {
        block = first_data_block_ + 1 + static_cast<std::uint64_t>(index);
    }
    if (block >= blocks_count_) return false;
    offset = (block << block_shift_) + static_cast<std::uint64_t>(group % per_block) * descriptor_bytes_;
    return true;
}

bool Volume::load_inode(std::uint32_t number, Inode& inode) {
    if (number == 0 || number > inodes_count_) return false;
    const auto group = (number - 1) / inodes_per_group_;
    const auto index = (number - 1) % inodes_per_group_;
    std::uint64_t descriptor_at = 0;
    if (!group_descriptor_offset(group, descriptor_at)) return false;
    std::uint8_t descriptor[64]{};
    const auto descriptor_length = descriptor_bytes_ < 64 ? descriptor_bytes_ : 64;
    if (!read_bytes(descriptor_at, descriptor, descriptor_length)) return false;
    // 64 位元卷冊的描述子把 inode 表位置的高 32 位元放在偏移 0x28。
    // A 64-bit volume's descriptor keeps the high 32 bits of the inode table's
    // location at offset 0x28.
    std::uint64_t table = le32(descriptor + 8);
    if (descriptor_bytes_ >= 64) table |= static_cast<std::uint64_t>(le32(descriptor + 0x28)) << 32;
    const auto table_blocks =
        (static_cast<std::uint64_t>(inodes_per_group_) * inode_bytes_ + block_bytes_ - 1) >> block_shift_;
    if (table == 0 || table >= blocks_count_ || table_blocks > blocks_count_ - table) return false;

    std::uint8_t raw[inode_core_bytes];
    if (!read_bytes((table << block_shift_) + static_cast<std::uint64_t>(index) * inode_bytes_, raw,
                    sizeof(raw)))
        return false;
    // 模式為零或連結數為零的 inode 沒有在使用。
    // An inode with no mode or no links is not in use.
    if (le16(raw + 0) == 0 || le16(raw + 26) == 0) return false;
    inode = Inode{};
    inode.number = number;
    inode.mode = le16(raw + 0);
    inode.flags = le32(raw + 32);
    inode.size = le32(raw + 4);
    // i_size_high 在一般檔案一定有效；目錄要有 largedir 才是大小的一部分，
    // 舊的 ext2 在那裡放的是別的東西。
    //
    // i_size_high always counts for a regular file; for a directory it is part
    // of the size only with largedir, since old ext2 kept something else there.
    if (inode.regular() || (incompatible_ & incompat_largedir) != 0)
        inode.size |= static_cast<std::uint64_t>(le32(raw + 108)) << 32;
    for (std::size_t i = 0; i < sizeof(inode.block); ++i) inode.block[i] = raw[40 + i];
    if ((inode.flags & inode_flag_inline_data) != 0) return false;

    // 短的符號連結把目標直接放在 i_block 裡（「快速」連結），這時它沒有任何
    // 資料區塊；i_blocks 只算到延伸屬性區塊，或者是零。
    //
    // A short symbolic link keeps its target right in i_block (a "fast" link)
    // and then owns no data block: i_blocks counts at most an extended
    // attribute block, or is zero.
    if (inode.symlink() && inode.size < symlink_inline_bytes &&
        (inode.flags & inode_flag_extents) == 0) {
        const auto blocks = le32(raw + 28);
        const auto attribute_block = le32(raw + 104);
        const auto attribute_blocks =
            attribute_block == 0 ? 0u
                                 : ((inode.flags & inode_flag_huge_file) != 0 ? 1u : block_bytes_ >> 9);
        inode.fast_symlink = blocks == attribute_blocks;
    }
    return true;
}

bool Volume::map_block(const Inode& inode, std::uint64_t logical, std::uint64_t& physical,
                       std::uint64_t& run) {
    if (logical >= logical_limit) {
        physical = 0;
        run = 1;
        return true;
    }
    if ((inode.flags & inode_flag_extents) != 0) return map_extent(inode, logical, physical, run);
    run = 1;
    return map_indirect(inode, logical, physical);
}

// 在 extent 樹裡找邏輯區塊。physical 為零表示讀起來是零（洞或未初始化的
// extent），run 是從 logical 起同樣情況延續的區塊數。
//
// Find a logical block in the extent tree. A physical of zero means it reads
// as zero (a hole or an uninitialized extent), and run is how many blocks from
// logical on are in the same situation.
bool Volume::map_extent(const Inode& inode, std::uint64_t logical, std::uint64_t& physical,
                        std::uint64_t& run) {
    if (extent_.inode == inode.number && logical >= extent_.logical &&
        logical - extent_.logical < extent_.length) {
        const auto skip = logical - extent_.logical;
        physical = extent_.physical == 0 ? 0 : extent_.physical + skip;
        run = extent_.length - skip;
        return true;
    }

    const std::uint8_t* node = inode.block;
    std::size_t node_bytes = sizeof(inode.block);
    // 目前這個節點涵蓋的範圍結束在哪裡：上一層下一個索引項目的起點。最後一個
    // extent 之後的洞只延伸到這裡。
    //
    // Where the range this node covers ends: the start of the next index entry
    // one level up. A hole after the last extent extends only this far.
    std::uint64_t limit = logical_limit;
    int expected_depth = -1;
    for (;;) {
        if (le16(node) != extent_magic) return false;
        const auto entries = le16(node + 2);
        const auto capacity = le16(node + 4);
        const auto depth = le16(node + 6);
        if (entries > capacity || extent_header_bytes + capacity * extent_entry_bytes > node_bytes ||
            depth > max_extent_depth || (expected_depth >= 0 && depth != expected_depth))
            return false;
        const auto* entry = node + extent_header_bytes;

        if (depth == 0) {
            std::uint64_t start = logical_limit;
            std::uint64_t length = 0;
            std::uint64_t target = 0;
            bool found = false;
            for (std::size_t i = 0; i < entries; ++i, entry += extent_entry_bytes) {
                const std::uint64_t first = le32(entry);
                const auto raw_length = le16(entry + 4);
                if (first > logical) {
                    // 項目依起點排序，第一個起點在 logical 之後的就是洞的盡頭。
                    // Entries are sorted by start, so the first one starting
                    // past logical is where the hole ends.
                    if (first < limit) limit = first;
                    break;
                }
                const bool initialized = raw_length <= max_initialized_extent;
                const std::uint64_t count = initialized ? raw_length : raw_length - max_initialized_extent;
                if (logical - first >= count) continue;
                const auto at = (static_cast<std::uint64_t>(le16(entry + 6)) << 32) | le32(entry + 8);
                if (at == 0 || at >= blocks_count_ || count > blocks_count_ - at) return false;
                start = first;
                length = count;
                target = initialized ? at : 0;
                found = true;
                break;
            }
            if (!found) {
                start = logical;
                length = limit - logical;
                target = 0;
            }
            extent_ = {inode.number, start, length, target};
            const auto skip = logical - start;
            physical = target == 0 ? 0 : target + skip;
            run = length - skip;
            return true;
        }

        // 索引節點：取最後一個起點不超過 logical 的項目往下走。
        // An index node: descend through the last entry starting at or before
        // logical.
        const std::uint8_t* chosen = nullptr;
        for (std::size_t i = 0; i < entries; ++i, entry += extent_entry_bytes) {
            const std::uint64_t first = le32(entry);
            if (first > logical) {
                if (first < limit) limit = first;
                break;
            }
            chosen = entry;
        }
        if (chosen == nullptr) {
            extent_ = {inode.number, logical, limit - logical, 0};
            physical = 0;
            run = limit - logical;
            return true;
        }
        const auto child = (static_cast<std::uint64_t>(le16(chosen + 8)) << 32) | le32(chosen + 4);
        if (child == 0 || child >= blocks_count_) return false;
        loaded_ = {};
        if (!read_bytes(child << block_shift_, block_, block_bytes_)) return false;
        node = block_;
        node_bytes = block_bytes_;
        expected_depth = depth - 1;
    }
}

// 舊式的區塊對應：12 個直接區塊，接著一、二、三層間接區塊。零是洞。
// The old block map: twelve direct blocks, then single, double and triple
// indirect blocks. Zero is a hole.
bool Volume::map_indirect(const Inode& inode, std::uint64_t logical, std::uint64_t& physical) {
    const std::uint64_t per_block = block_bytes_ / 4;
    // 從間接區塊 table 讀出第 index 個指標。 / Read pointer index from the
    // indirect block table.
    const auto pointer = [this](std::uint64_t table, std::uint64_t index, std::uint64_t& value) {
        if (table == 0) {
            value = 0;
            return true;
        }
        if (table >= blocks_count_) return false;
        std::uint8_t bytes[4];
        if (!read_bytes((table << block_shift_) + index * 4, bytes, sizeof(bytes))) return false;
        value = le32(bytes);
        return true;
    };

    std::uint64_t value = 0;
    if (logical < direct_blocks) {
        value = le32(inode.block + 4 * logical);
    } else {
        logical -= direct_blocks;
        int levels = 1;
        std::uint64_t span = per_block;
        while (logical >= span) {
            logical -= span;
            if (++levels > 3) {
                physical = 0;
                return true;
            }
            span *= per_block;
        }
        value = le32(inode.block + 4 * (direct_blocks - 1 + levels));
        for (; levels > 0; --levels) {
            span /= per_block;
            if (!pointer(value, logical / span, value)) return false;
            logical %= span;
        }
    }
    if (value >= blocks_count_) return false;
    physical = value;
    return true;
}

io::Result Volume::read_inode(const Inode& inode, std::uint64_t offset, void* buffer,
                              std::size_t length) {
    if (offset >= inode.size) return {0, io::Error::None};
    if (length > inode.size - offset) length = static_cast<std::size_t>(inode.size - offset);
    auto* out = static_cast<std::uint8_t*>(buffer);
    if (inode.fast_symlink) {
        for (std::size_t i = 0; i < length; ++i) out[i] = inode.block[offset + i];
        return {length, io::Error::None};
    }
    std::size_t done = 0;
    while (done < length) {
        const auto at = offset + done;
        const auto within = at & (block_bytes_ - 1);
        std::uint64_t physical = 0;
        std::uint64_t run = 0;
        if (!map_block(inode, at >> block_shift_, physical, run)) return {done, io::Error::DeviceError};
        const auto remaining = length - done;
        const auto available = (run << block_shift_) - within;
        const auto take = available < remaining ? static_cast<std::size_t>(available) : remaining;
        if (physical == 0) {
            for (std::size_t i = 0; i < take; ++i) out[done + i] = 0;
        } else {
            const auto result = read_bytes((physical << block_shift_) + within, out + done, take);
            if (!result) return {done + result.transferred, result.error};
        }
        done += take;
    }
    return {done, io::Error::None};
}

// 從目錄內容的 position 起取出下一個項目，position 前進到它之後。空的項目、
// "." 與 ".." 都被跳過；格式不對的項目讓掃描停下來，而不是猜下一個項目在哪裡。
//
// Take the next entry from position in the directory's content, advancing
// position past it. Empty entries, "." and ".." are skipped; a malformed entry
// stops the scan rather than guessing where the next one starts.
bool Volume::next_entry(const Inode& directory, std::uint64_t& position, Entry& entry) {
    if ((directory.flags & inode_flag_encrypt) != 0) return false;
    while (position < directory.size) {
        const auto logical = position >> block_shift_;
        const auto within = static_cast<std::size_t>(position & (block_bytes_ - 1));
        if (loaded_.inode != directory.number || loaded_.logical != logical) {
            std::uint64_t physical = 0;
            std::uint64_t run = 0;
            if (!map_block(directory, logical, physical, run)) return false;
            if (physical == 0) {
                // 目錄裡的洞沒有項目。 / A hole in a directory holds no entries.
                position = (logical + 1) << block_shift_;
                continue;
            }
            loaded_ = {};
            if (!read_bytes(physical << block_shift_, block_, block_bytes_)) return false;
            loaded_ = {directory.number, logical};
        }
        if (within + 8 > block_bytes_) return false;
        const auto* raw = block_ + within;
        const auto number = le32(raw);
        std::size_t record = le16(raw + 4);
        // 64 KiB 的區塊放不進 16 位元的 rec_len，低兩位元借來當高位元。
        // A 64 KiB block does not fit a 16-bit rec_len, so the two low bits
        // stand in for the high ones.
        if (block_bytes_ == 65536)
            record = (record == 65535 || record == 0) ? 65536 : (record & 65532) | ((record & 3) << 16);
        // 沒有 filetype 功能時名稱長度是 16 位元；有的話高位元組是檔案類型。
        // Without the filetype feature the name length is 16 bits; with it, the
        // high byte is the file type.
        const std::size_t name_length =
            (incompatible_ & incompat_filetype) != 0 ? raw[6] : le16(raw + 6);
        if (record < 8 || (record & 3) != 0 || within + record > block_bytes_ ||
            name_length > 255 || 8 + name_length > record)
            return false;
        position += record;
        if (number == 0 || name_length == 0 || number > inodes_count_) continue;
        const auto* name = reinterpret_cast<const char*>(raw + 8);
        if ((name_length == 1 && name[0] == '.') ||
            (name_length == 2 && name[0] == '.' && name[1] == '.'))
            continue;
        bool legal = true;
        for (std::size_t i = 0; legal && i < name_length; ++i) legal = name[i] != '/' && name[i] != '\0';
        if (!legal) continue;
        entry.inode = number;
        entry.name_length = static_cast<std::uint8_t>(name_length);
        for (std::size_t i = 0; i < name_length; ++i) entry.name[i] = name[i];
        entry.name[name_length] = '\0';
        return true;
    }
    return false;
}

// 只有目錄、一般檔案與符號連結變成節點；裝置檔、FIFO 與 socket 在這裡沒有
// 意義，因此不列出來。
//
// Only directories, regular files and symbolic links become nodes; device
// files, FIFOs and sockets mean nothing here, so they are not listed.
bool Volume::to_node(const Inode& inode, const Entry& entry, vfs::Node& node) const {
    if (!inode.directory() && !inode.regular() && !inode.symlink()) return false;
    node = vfs::Node{};
    node.filesystem = const_cast<Volume*>(this);
    node.id = inode.number;
    node.type = inode.directory() ? vfs::Type::Directory : vfs::Type::File;
    node.size = inode.size;
    return text::copy(node.name, sizeof(node.name), entry.name);
}

bool Volume::decode(const vfs::Node& node, Inode& inode) {
    if (!mounted() || node.filesystem != this || node.id == 0 || node.id > inodes_count_) return false;
    return load_inode(static_cast<std::uint32_t>(node.id), inode);
}

bool Volume::root(vfs::Node& node) {
    Inode inode;
    if (!mounted() || !load_inode(root_inode, inode)) return false;
    node = vfs::Node{};
    node.filesystem = this;
    node.id = root_inode;
    node.type = vfs::Type::Directory;
    node.size = inode.size;
    return true;
}

// 名稱以位元組比較，區分大小寫。casefold 目錄在 Linux 上不分大小寫，這裡
// 則必須打出存在磁碟上的那個寫法。
//
// Names compare byte for byte, case-sensitively. A casefold directory ignores
// case on Linux; here the spelling stored on disk has to be used.
bool Volume::lookup(const vfs::Node& directory, const char* name, vfs::Node& result) {
    Inode parent;
    if (!decode(directory, parent) || !parent.directory()) return false;
    const auto length = text::length(name);
    if (length == 0 || length > 255) return false;
    Entry entry;
    for (std::uint64_t position = 0; next_entry(parent, position, entry);) {
        if (entry.name_length != length || !text::equals(entry.name, name)) continue;
        Inode inode;
        return load_inode(entry.inode, inode) && to_node(inode, entry, result);
    }
    return false;
}

bool Volume::list(const vfs::Node& directory, std::size_t position, vfs::Node& child) {
    Inode parent;
    if (!decode(directory, parent) || !parent.directory()) return false;
    Entry entry;
    std::size_t index = 0;
    std::uint64_t at = 0;
    if (listing_.inode == parent.number && listing_.index <= position) {
        index = listing_.index;
        at = listing_.position;
    }
    while (next_entry(parent, at, entry)) {
        // 打不開的項目（inode 壞了、類型不支援）不列出來，與 lookup() 一致。
        // An entry that cannot be opened (a bad inode, an unsupported type) is
        // not listed, agreeing with lookup().
        Inode inode;
        if (!load_inode(entry.inode, inode) || !to_node(inode, entry, child)) continue;
        if (index++ == position) {
            listing_ = {parent.number, index, at};
            return true;
        }
    }
    return false;
}

io::Result Volume::read(const vfs::Node& file, std::uint64_t offset, void* buffer,
                        std::size_t length) {
    Inode inode;
    if (!decode(file, inode) || inode.directory()) return {0, io::Error::InvalidArgument};
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (!inode.regular() && !inode.symlink()) return {0, io::Error::Unsupported};
    if ((inode.flags & inode_flag_encrypt) != 0) return {0, io::Error::Unsupported};
    return read_inode(inode, offset, buffer, length);
}

io::Result Volume::write(const vfs::Node&, std::uint64_t, const void*, std::size_t) {
    return {0, io::Error::ReadOnly};
}

} // namespace shirley::ext4
