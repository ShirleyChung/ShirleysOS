#pragma once

#include "shirley/block_device.hpp"
#include "shirley/vfs.hpp"

#include <cstddef>
#include <cstdint>

// ext2／ext3／ext4，依照 Linux 核心文件的 ext4 磁碟格式說明實作，唯讀。
//
//     超級區塊      位元組 1024 起，卷冊的大小與功能旗標
//     區塊群組      每個群組有自己的位元圖與 inode 表；群組描述子表說出它們在哪裡
//     inode         檔案的類型、大小，以及 60 位元組的 i_block：extent 樹的根，
//                   或舊式的 12 個直接區塊加上一、二、三層間接區塊
//     目錄          一串可變長度的項目（inode 編號、名稱）；htree 索引藏在
//                   假的空項目後面，循序掃描時自然會跳過
//
// 三個世代用同一個驅動程式：ext2 只是沒有 journal 與 extent 的 ext4。讀取時
// journal 被忽略，因此 journal 還有沒重播的交易（needs_recovery）的卷冊會被拒絕：
// 那時磁碟上的 metadata 可能是舊的。
//
// 不支援的功能會讓掛載失敗而不是讀出錯的東西：inline_data、bigalloc、
// 外部 journal、dirdata、壓縮。加密的檔案與目錄看得到但讀不出內容。
// metadata_csum 卷冊的超級區塊校驗和會被檢查；其他 metadata 只做範圍檢查。
//
// ext2, ext3 and ext4, implemented from the ext4 disk layout description in the
// Linux kernel documentation, read-only.
//
//     superblock     from byte 1024: the volume's size and feature flags
//     block groups   each has its own bitmaps and inode table, found through
//                    the group descriptor table
//     inode          a file's type and size, and the 60-byte i_block: the root
//                    of an extent tree, or the old twelve direct blocks plus
//                    single, double and triple indirect blocks
//     directory      a run of variable-length entries (inode number, name);
//                    the htree index hides behind fake empty entries, which a
//                    linear scan skips anyway
//
// One driver covers all three generations: ext2 is merely ext4 without a
// journal or extents. The journal is ignored when reading, so a volume whose
// journal still holds transactions to replay (needs_recovery) is refused: its
// metadata on disk may be stale until then.
//
// A feature this driver does not support makes the mount fail rather than read
// the wrong thing: inline_data, bigalloc, an external journal, dirdata,
// compression. Encrypted files and directories show up but their content
// cannot be read. The superblock checksum of a metadata_csum volume is checked;
// other metadata is only bounds-checked.
namespace shirley::ext4 {

// mount() 的失敗原因。 / Why mount() failed.
enum class MountError : std::uint8_t {
    None,
    NotExt4,
    Unsupported,
    NeedsRecovery,
    Corrupt,
    DeviceError,
    OutOfMemory,
    AlreadyMounted,
};
const char* mount_error_text(MountError error);

// 只看超級區塊的魔數。 / Look at the superblock's magic number and nothing else.
bool identify(io::BlockDevice& device);

class Volume final : public vfs::FileSystem {
public:
    constexpr Volume() = default;

    // 驗證超級區塊（含功能旗標與校驗和）並確認根目錄是目錄。任何一步失敗都
    // 維持未掛載狀態。上限：裝置區塊不超過 4 KiB，檔案系統區塊不超過 64 KiB。
    //
    // Validate the superblock (feature flags and checksum included) and check
    // that the root is a directory. Any failure leaves the volume unmounted.
    // Limits: device blocks of at most 4 KiB, file system blocks of at most
    // 64 KiB.
    MountError mount(io::BlockDevice& device);
    void unmount();
    bool mounted() const { return device_ != nullptr; }

    // 卷冊標籤與幾個超級區塊欄位，供診斷使用。
    // The volume label and a few superblock fields, for diagnostics.
    const char* label() const { return label_; }
    std::size_t block_size() const { return block_bytes_; }
    std::uint32_t group_count() const { return group_count_; }
    // 上次沒有乾淨地卸載，或者 e2fsck 發現過錯誤。唯讀掛載不受影響。
    // Not cleanly unmounted last time, or e2fsck found errors once. A read-only
    // mount does not care.
    bool dirty() const { return dirty_; }

    // 依功能旗標稱為 ext2、ext3 或 ext4，並標上 (ro)。
    // Called ext2, ext3 or ext4 by its feature flags, marked (ro).
    const char* name() const override { return name_; }
    bool root(vfs::Node& node) override;
    bool lookup(const vfs::Node& directory, const char* name, vfs::Node& result) override;
    bool list(const vfs::Node& directory, std::size_t position, vfs::Node& child) override;
    // 符號連結以檔案呈現，內容是連結的目標。
    // A symbolic link appears as a file whose content is the link's target.
    io::Result read(const vfs::Node& file, std::uint64_t offset, void* buffer,
                    std::size_t length) override;
    io::Result write(const vfs::Node& file, std::uint64_t offset, const void* buffer,
                     std::size_t length) override;
    bool writable(const vfs::Node&) const override { return false; }

    // 一個解碼後的 inode。供實作與測試使用。
    // One decoded inode. For the implementation and tests.
    struct Inode {
        std::uint32_t number = 0;
        std::uint16_t mode = 0;
        std::uint32_t flags = 0;
        std::uint64_t size = 0;
        std::uint8_t block[60]{};
        // 符號連結的目標直接放在 block 裡。 / A symbolic link whose target sits
        // right in block.
        bool fast_symlink = false;
        std::uint16_t type() const { return mode & 0xF000; }
        bool directory() const { return type() == 0x4000; }
        bool regular() const { return type() == 0x8000; }
        bool symlink() const { return type() == 0xA000; }
    };

private:
    // 一個目錄項目。 / One directory entry.
    struct Entry {
        std::uint32_t inode = 0;
        std::uint8_t name_length = 0;
        char name[256]{};
    };

    void release();
    io::Result read_bytes(std::uint64_t offset, void* buffer, std::size_t length);
    bool has_superblock(std::uint32_t group) const;
    bool group_descriptor_offset(std::uint32_t group, std::uint64_t& offset) const;
    bool load_inode(std::uint32_t number, Inode& inode);
    bool map_block(const Inode& inode, std::uint64_t logical, std::uint64_t& physical,
                   std::uint64_t& run);
    bool map_extent(const Inode& inode, std::uint64_t logical, std::uint64_t& physical,
                    std::uint64_t& run);
    bool map_indirect(const Inode& inode, std::uint64_t logical, std::uint64_t& physical);
    io::Result read_inode(const Inode& inode, std::uint64_t offset, void* buffer,
                          std::size_t length);
    bool next_entry(const Inode& directory, std::uint64_t& position, Entry& entry);
    bool to_node(const Inode& inode, const Entry& entry, vfs::Node& node) const;
    bool decode(const vfs::Node& node, Inode& inode);

    io::BlockDevice* device_ = nullptr;
    // 裝置區塊大小的跳板緩衝區，以及一個檔案系統區塊大小的緩衝區，給目錄與
    // extent 樹的節點用。兩者都從堆積配置。
    //
    // A bounce buffer of one device block, and a buffer of one file system
    // block for directory blocks and extent tree nodes. Both come from the
    // heap.
    std::uint8_t* bounce_ = nullptr;
    std::uint8_t* block_ = nullptr;
    std::uint64_t blocks_count_ = 0;
    std::uint32_t block_bytes_ = 0;
    std::uint32_t block_shift_ = 0;
    std::uint32_t inodes_count_ = 0;
    std::uint32_t inodes_per_group_ = 0;
    std::uint32_t blocks_per_group_ = 0;
    std::uint32_t group_count_ = 0;
    std::uint32_t first_data_block_ = 0;
    std::uint32_t first_meta_bg_ = 0;
    std::uint32_t backup_groups_[2]{};
    std::uint16_t inode_bytes_ = 0;
    std::uint16_t descriptor_bytes_ = 0;
    std::uint32_t compatible_ = 0;
    std::uint32_t incompatible_ = 0;
    std::uint32_t read_only_compatible_ = 0;
    bool dirty_ = false;
    const char* name_ = "ext4 (ro)";
    char label_[17]{};
    // block_ 目前裝著哪個 inode 的哪個邏輯區塊；目錄被循序掃描時，同一個區塊
    // 不必一讀再讀。extent 節點也放在 block_ 裡，因此讀節點時清掉這個記錄。
    //
    // Which logical block of which inode block_ holds now, so scanning a
    // directory in order does not read the same block again and again. Extent
    // nodes also land in block_, so reading one clears this record.
    struct {
        std::uint32_t inode = 0;
        std::uint64_t logical = 0;
    } loaded_{};
    // 上一次 list() 停在哪裡：第 index 個項目之後，目錄內容的 position 位元組處。
    // `ls` 依序要第 0、1、2… 個項目，從這裡接著掃描，列一個目錄就不必是平方
    // 時間。卷冊唯讀，記錄不會過期。
    //
    // Where the last list() stopped: after entry index, at byte position of the
    // directory's content. `ls` asks for entries 0, 1, 2… in order, and resuming
    // here keeps listing a directory from taking quadratic time. The volume is
    // read-only, so the record cannot go stale.
    struct {
        std::uint32_t inode = 0;
        std::size_t index = 0;
        std::uint64_t position = 0;
    } listing_{};
    // 最近一次對應到的 extent。循序讀取一個檔案時，下一個區塊幾乎總是在同一個
    // extent 裡，不必從樹根重新走一次。
    //
    // The extent last mapped. Reading a file in order, the next block is nearly
    // always in the same extent, with no need to walk down from the root again.
    struct {
        std::uint32_t inode = 0;
        std::uint64_t logical = 0;
        std::uint64_t length = 0;
        std::uint64_t physical = 0;
    } extent_{};
};

} // namespace shirley::ext4
