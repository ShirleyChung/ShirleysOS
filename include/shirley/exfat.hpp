#pragma once

#include "shirley/block_device.hpp"
#include "shirley/vfs.hpp"

#include <cstddef>
#include <cstdint>

// exFAT，依照 Microsoft 公開的 exFAT 檔案系統規格實作，可讀可寫。
//
//     boot region   開機磁區、延伸開機磁區、OEM 參數、開機校驗和；之後是備份
//     FAT           每個叢集 32 位元的下一個叢集；連續檔案可以完全不用它
//     cluster heap  叢集 2 開始的資料區，根目錄、配置位元圖、大寫表都在這裡
//
// 一個檔案是一組連續的 32 位元組目錄項目：File 項目（屬性與時間）、Stream
// Extension 項目（第一個叢集、長度、是否不經 FAT 的連續配置），再加上一到
// 十七個 File Name 項目，每個裝 15 個 UTF-16 單元。整組有自己的校驗和。
//
// 與 SHRFS 不同，這是一個物件：每個卷冊一個實例，因此可以同時掛載好幾個。
// 所有存取都經過 io::BlockDevice；建議在中間放一個 io::BlockCache，FAT 與目錄
// 項目會被一讀再讀。
//
// exFAT, implemented from Microsoft's published exFAT file system
// specification, for reading and writing.
//
//     boot region   boot sector, extended boot sectors, OEM parameters, boot
//                   checksum; a backup copy follows
//     FAT           a 32-bit next-cluster value per cluster; a contiguous file
//                   need not use it at all
//     cluster heap  the data area from cluster 2 on, holding the root
//                   directory, the allocation bitmap and the up-case table
//
// A file is a run of 32-byte directory entries: a File entry (attributes and
// times), a Stream Extension entry (first cluster, length, and whether the
// allocation is contiguous and bypasses the FAT), then one to seventeen File
// Name entries of 15 UTF-16 units each. The run has a checksum of its own.
//
// Unlike SHRFS this is an object: one instance per volume, so several can be
// mounted at once. Every access goes through io::BlockDevice; an io::BlockCache
// in between is recommended, since the FAT and directory entries are read over
// and over.
namespace shirley::exfat {

// mount() 的失敗原因，讓 shell 說得出「這不是 exFAT」與「這是壞掉的 exFAT」
// 的差別。
//
// Why mount() failed, so a shell can tell "this is not exFAT" apart from "this
// is exFAT, but damaged".
enum class MountError : std::uint8_t {
    None,
    NotExfat,
    Unsupported,
    Corrupt,
    DeviceError,
    OutOfMemory,
    AlreadyMounted,
};
const char* mount_error_text(MountError error);

// 只看開機磁區的簽章，不驗證其他任何東西；用來在多種格式之間挑一個。
// Look at the boot sector's signature and nothing else; for picking one format
// among several.
bool identify(io::BlockDevice& device);

class Volume final : public vfs::FileSystem {
public:
    constexpr Volume() = default;

    // 驗證開機區（含校驗和）、讀出根目錄、載入並檢查大寫表。任何一步失敗都
    // 維持未掛載狀態。上限：裝置區塊與磁區都不超過 4 KiB。
    //
    // writable 要求可寫的掛載。卷冊上次沒有乾淨地卸載（VolumeDirty）、有兩份
    // FAT（TexFAT），或裝置拒絕寫入時，仍然會掛載，但是唯讀——read_only() 說得出
    // 是哪一種。可寫的掛載會立刻把 VolumeDirty 寫到裝置上，直到 unmount() 才清掉，
    // 所以中途斷電的卷冊下次會被認出來。
    //
    // Validate the boot region (checksum included), read the root directory,
    // and load and check the up-case table. Any failure leaves the volume
    // unmounted. Limits: device blocks and sectors of at most 4 KiB.
    //
    // writable asks for a writable mount. A volume that was not cleanly
    // unmounted last time (VolumeDirty), one with two FATs (TexFAT), or a device
    // that refuses writes is still mounted, but read-only — read_only() tells.
    // A writable mount puts VolumeDirty on the device at once and clears it
    // only in unmount(), so a volume interrupted by a power cut is recognised
    // next time.
    MountError mount(io::BlockDevice& device, bool writable = false);
    // 寫回所有修改、清掉 VolumeDirty，然後釋放一切。
    // Write back every modification, clear VolumeDirty, then release
    // everything.
    void unmount();
    bool mounted() const { return device_ != nullptr; }
    bool read_only() const { return !writable_; }

    // 卷冊標籤（UTF-8，沒有標籤時為空字串）與幾個開機磁區欄位，供診斷使用。
    // The volume label (UTF-8, empty when there is none) and a few boot sector
    // fields, for diagnostics.
    const char* label() const { return label_; }
    std::uint32_t serial_number() const { return serial_; }
    std::uint32_t cluster_count() const { return cluster_count_; }
    std::size_t cluster_size() const { return std::size_t{1} << cluster_shift_; }
    // 上次沒有乾淨地卸載。唯讀掛載不受影響，但之後的寫入支援要看它。
    // The volume was not cleanly unmounted last time. A read-only mount does
    // not care, but write support later will.
    bool dirty() const { return dirty_; }

    // 唯讀掛載時名稱標上 (ro)，`mount` 列出來才看得出差別。
    // A read-only mount is named with (ro), so `mount` shows the difference.
    const char* name() const override { return writable_ ? "exfat" : "exfat (ro)"; }
    bool root(vfs::Node& node) override;
    bool lookup(const vfs::Node& directory, const char* name, vfs::Node& result) override;
    bool list(const vfs::Node& directory, std::size_t position, vfs::Node& child) override;
    io::Result read(const vfs::Node& file, std::uint64_t offset, void* buffer,
                    std::size_t length) override;
    io::Result write(const vfs::Node& file, std::uint64_t offset, const void* buffer,
                     std::size_t length) override;
    bool writable(const vfs::Node& node) const override;
    io::Error create(const vfs::Node& directory, const char* name, vfs::Type type,
                     vfs::Node& result) override;
    io::Error remove(const vfs::Node& directory, const char* name) override;
    io::Error rename(const vfs::Node& from_directory, const char* from_name,
                     const vfs::Node& to_directory, const char* to_name) override;
    io::Error truncate(const vfs::Node& file, std::uint64_t size) override;
    io::Error sync() override;

    // 一串叢集：檔案或目錄的內容。供實作與測試使用。
    // A run of clusters: the content of a file or directory. For the
    // implementation and tests.
    struct Stream {
        std::uint32_t first_cluster = 0;
        std::uint64_t length = 0;
        std::uint64_t valid_length = 0;
        bool contiguous = false;
    };

    // 解碼後的一組目錄項目。
    // One decoded set of directory entries.
    struct Entry {
        Stream stream;
        std::uint16_t attributes = 0;
        std::uint16_t name_hash = 0;
        std::uint8_t name_length = 0;
        char16_t name[255]{};
        // 這組項目在父目錄內容中的位置，以 32 位元組為單位。
        // Where the set sits within its parent's content, in 32-byte units.
        std::uint32_t slot = 0;
        bool directory() const { return (attributes & 0x10) != 0; }
    };

private:
    // 一個節點在磁碟上的位置：它所在的目錄，以及它自己的項目組。根目錄沒有
    // 項目組，root 為 true。
    //
    // Where a node lives on disk: the directory holding it, and its own entry
    // set. The root directory has no entry set, and root is true.
    struct Location {
        Stream parent;
        Entry entry;
        bool root = false;
    };

    void release();
    bool locate(const vfs::Node& node, Location& where);
    bool find(const Stream& directory, const char* name, Entry& entry);
    std::uint16_t name_hash(const char16_t* name, std::size_t length) const;

    io::Result write_bytes(std::uint64_t offset, const void* buffer, std::size_t length);
    io::Result write_stream(const Stream& stream, std::uint64_t offset, const void* buffer,
                            std::size_t length);
    io::Error zero_stream(const Stream& stream, std::uint64_t offset, std::uint64_t length);
    bool fat_set(std::uint32_t cluster, std::uint32_t value);
    bool bitmap_test(std::uint32_t cluster, bool& used);
    bool bitmap_set(std::uint32_t cluster, bool used);
    io::Error allocate_cluster(std::uint32_t prefer, std::uint32_t& cluster);
    io::Error release_clusters(Stream& stream, std::uint64_t keep, std::uint64_t current);
    io::Error resize(Stream& stream, std::uint64_t clusters);
    io::Error store(const Stream& parent, const Entry& entry);
    io::Error insert(const vfs::Node& directory, std::uint8_t* set, std::size_t entries,
                     Stream& parent, std::uint32_t& slot);
    io::Error erase(const Stream& parent, std::uint32_t slot);
    bool set_volume_flags(bool dirty);

    io::Result read_bytes(std::uint64_t offset, void* buffer, std::size_t length);
    bool fat_next(std::uint32_t cluster, std::uint32_t& next);
    bool cluster_at(const Stream& stream, std::uint64_t index, std::uint32_t& cluster);
    io::Result read_stream(const Stream& stream, std::uint64_t offset, void* buffer,
                           std::size_t length);
    bool next_entry(const Stream& directory, std::uint32_t& slot, Entry& entry);
    bool valid_name(const Entry& entry) const;
    bool decode(const vfs::Node& node, Stream& stream, bool& directory);
    bool to_node(const Stream& parent, const Entry& entry, vfs::Node& node);
    bool load_upcase(const Stream& table, std::uint32_t checksum);
    char16_t upcase(char16_t unit) const;

    io::BlockDevice* device_ = nullptr;
    // 裝置區塊大小的跳板緩衝區，從堆積配置。
    // A bounce buffer of one device block, allocated from the heap.
    std::uint8_t* bounce_ = nullptr;
    // 展開後的大寫表，65536 項；從堆積配置。
    // The expanded up-case table, 65536 entries, allocated from the heap.
    char16_t* upcase_ = nullptr;
    std::uint64_t fat_offset_ = 0;
    std::uint64_t heap_offset_ = 0;
    std::uint32_t cluster_count_ = 0;
    std::uint32_t cluster_shift_ = 0;
    std::uint32_t serial_ = 0;
    Stream root_{};
    // 配置位元圖本身的位置。 / Where the allocation bitmap itself lives.
    Stream bitmap_{};
    std::uint16_t volume_flags_ = 0;
    // 找空叢集時從這裡開始，免得每次都從頭掃描位元圖。
    // Where the search for a free cluster starts, so the bitmap is not scanned
    // from the beginning every time.
    std::uint32_t allocation_hint_ = 2;
    bool dirty_ = false;
    bool writable_ = false;
    char label_[3 * 11 + 1]{};
    // 最近一次 cluster_at() 走到的位置。循序讀取一個靠 FAT 串起來的檔案時，
    // 下一次就從這裡接著走，而不是每次都從第一個叢集重新數。
    //
    // Where the last cluster_at() walk ended. Reading a FAT-chained file in
    // order resumes from here instead of counting from the first cluster
    // every time.
    struct {
        std::uint32_t first_cluster = 0;
        std::uint64_t index = 0;
        std::uint32_t cluster = 0;
    } cursor_{};
};

} // namespace shirley::exfat
