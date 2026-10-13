#pragma once

#include "shirley/block_device.hpp"
#include "shirley/device.hpp"

#include <cstddef>
#include <cstdint>

// 分割表：MBR（含延伸分割區裡的邏輯分割區）與 GPT。
//
// 一個磁碟登記到裝置註冊表之後，驅動程式呼叫 publish()，每個分割區就變成一個
// 自己的區塊裝置——`vda` 底下的 `vda1`、`vda2`，名字以數字結尾的磁碟則是
// `ram0p1`，與 Linux 的慣例相同。分割區只是磁碟上一段連續區塊的視窗，檔案系統
// 掛在它上面跟掛在整顆磁碟上沒有差別。
//
// 沒有分割表的磁碟（整顆格式化成一個檔案系統的隨身碟就是如此）不產生任何
// 分割區，直接掛載整顆磁碟即可。
//
// Partition tables: MBR (with the logical partitions inside an extended one)
// and GPT.
//
// Once a disk is in the device registry its driver calls publish(), and each
// partition becomes a block device of its own — `vda1` and `vda2` under `vda`,
// or `ram0p1` for a disk whose name ends in a digit, as Linux names them. A
// partition is merely a window onto a run of the disk's blocks, and a file
// system mounted on one cannot tell it from a whole disk.
//
// A disk with no partition table — a USB stick formatted as one file system
// end to end is like that — yields no partitions; mount the whole disk.
namespace shirley::io {

// 另一個區塊裝置上的一段連續區塊。區塊編號從視窗的開頭算起，越界的請求在
// 碰到底下的裝置之前就被拒絕。
//
// A run of consecutive blocks on another block device. Block numbers count
// from the start of the window, and a request past its end is refused before it
// reaches the device underneath.
class Partition final : public BlockDevice {
public:
    constexpr Partition() = default;
    void attach(BlockDevice& disk, std::uint64_t first, std::uint64_t count);
    std::uint64_t first_block() const { return first_; }

    std::size_t block_size() const override;
    std::uint64_t block_count() const override { return disk_ != nullptr ? count_ : 0; }
    Result read_blocks(std::uint64_t first, std::size_t count, void* buffer) override;
    Result write_blocks(std::uint64_t first, std::size_t count, const void* buffer) override;
    Result flush() override { return disk_ != nullptr ? disk_->flush() : Result{0, Error::None}; }

private:
    BlockDevice* disk_ = nullptr;
    std::uint64_t first_ = 0;
    std::uint64_t count_ = 0;
};

} // namespace shirley::io

namespace shirley::partition {

enum class Scheme : std::uint8_t { None, Mbr, Gpt };
const char* scheme_name(Scheme scheme);

// 一個分割區。位置與大小以磁碟的區塊為單位。MBR 分割區帶著它的類型位元組，
// GPT 分割區帶著類型 GUID；另一種的欄位為零。
//
// One partition, placed and sized in the disk's blocks. An MBR partition
// carries its type byte and a GPT partition its type GUID; the other field is
// zero.
struct Entry {
    // 分割區編號，也就是裝置名稱結尾的那個數字：MBR 的主要分割區是 1 到 4（依
    // 表中的位置，可以有空號），邏輯分割區從 5 開始；GPT 是項目表的索引加一。
    //
    // The partition number, the digit that ends the device name: an MBR primary
    // is 1 to 4 by its slot in the table (gaps allowed) and logical partitions
    // start at 5; a GPT partition is its entry array index plus one.
    std::uint32_t number = 0;
    std::uint64_t first = 0;
    std::uint64_t count = 0;
    std::uint8_t mbr_type = 0;
    std::uint8_t type_guid[16]{};
};

// 一張表最多記錄的分割區數。更多的分割區不會被丟掉而不說：truncated 會被設起來。
// The most partitions one table records. More are not dropped silently:
// truncated is set.
constexpr std::size_t max_entries = 16;

struct Table {
    Scheme scheme = Scheme::None;
    std::size_t count = 0;
    bool truncated = false;
    // GPT 的主要標頭壞掉、改用了磁碟最後一個區塊的備份。
    // The primary GPT header was damaged and the backup in the disk's last
    // block was used instead.
    bool used_backup = false;
    Entry entries[max_entries];
};

// 讀出磁碟的分割表。沒有分割表時回傳 true，scheme 為 None。分割表存在卻壞掉
// （GPT 的主要與備份標頭都不對）或裝置讀取失敗時回傳 false。
//
// Read a disk's partition table. With no table, returns true and scheme None.
// Returns false when a table exists but is damaged (both GPT headers bad) or a
// device read fails.
bool read(io::BlockDevice& disk, Table& table);

// 系統裡同時可以存在的分割區裝置數。
// How many partition devices can exist across the system at once.
constexpr std::size_t max_devices = 16;

// 讀出已登記的區塊裝置 disk 的分割表，並把每個分割區登記成一個裝置，回傳
// 登記了幾個。disk 的名字太長、放不下分割區編號時，該分割區被略過。
//
// Read the partition table of disk, a registered block device, register each
// partition as a device, and report how many were registered. A partition is
// skipped when the disk's name leaves no room for the partition number.
std::size_t publish(device::Device& disk);

// 取消登記 publish() 為 disk 登記的所有分割區。
// Unregister every partition publish() registered for disk.
void unpublish(device::Device& disk);

} // namespace shirley::partition
