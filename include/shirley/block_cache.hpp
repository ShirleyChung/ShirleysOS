#pragma once

#include "shirley/block_device.hpp"

#include <cstddef>
#include <cstdint>

// 區塊快取：把最近用過的區塊留在記憶體裡，寫入先留在快取，等 sync() 或區塊
// 被擠出去時才寫回裝置。
//
// 它本身也是一個 BlockDevice，因此檔案系統不需要知道快取存在：SHRFS 掛在
// 快取上與掛在裝置上，看到的是同樣的位元組。需要直接改快取內容的檔案系統
// （之後 exFAT 與 ext4 修改 metadata 時）改用 acquire()/release()，免去一次
// 複製，同時被 acquire 住的區塊不會被擠出去。
//
// 快取的區塊可以比裝置的區塊大，只要是整數倍：ext4 以 4 KiB 為單位思考，
// 底下的磁碟卻以 512 位元組定址，這個換算在這裡做一次就好。
//
// 同一個裝置只應該透過一個快取存取。繞過快取直接寫裝置的話，快取裡的那份
// 會變成舊資料，而快取沒有辦法知道。
//
// A block cache: recently used blocks stay in memory, and a write stays in the
// cache until sync() or until the block is evicted, when it goes back to the
// device.
//
// It is a BlockDevice itself, so a file system need not know it is there:
// SHRFS mounted on the cache sees the same bytes as SHRFS mounted on the
// device. A file system that wants to change cached content in place (exFAT
// and ext4 editing metadata later) uses acquire()/release() instead, which
// saves a copy and keeps the acquired block from being evicted meanwhile.
//
// A cached block may be larger than the device's block, as long as it is a
// whole multiple: ext4 thinks in 4 KiB units while the disk underneath is
// addressed in 512 bytes, and the conversion is done once, here.
//
// A device should be reached through one cache only. Writing the device
// directly behind the cache's back leaves the cached copy stale, and the cache
// has no way of knowing.
namespace shirley::io {

class BlockCache final : public BlockDevice {
public:
    // acquire() 的用途。Read 從裝置載入內容；Overwrite 表示呼叫端會覆寫整個
    // 區塊，所以不必先讀，緩衝區被清成零，區塊立刻被標成需要寫回。
    //
    // What acquire() is for. Read loads the content from the device. Overwrite
    // says the caller will replace the whole block, so nothing is read first:
    // the buffer is zeroed and the block is marked for write-back at once.
    enum class Access : std::uint8_t { Read, Overwrite };

    // 建構不配置任何東西，因此快取可以是 constinit 物件；核心不執行全域建構子。
    // Construction allocates nothing, so a cache can be a constinit object; the
    // kernel runs no global constructors.
    explicit constexpr BlockCache(BlockDevice& device) : device_(&device) {}

    // 從堆積配置 capacity 個區塊的緩衝區。block_bytes 為 0 時沿用裝置的區塊
    // 大小，否則必須是它的整數倍。堆積不夠、參數不合法或已經初始化過時回傳
    // false。
    //
    // Allocate buffers for capacity blocks from the heap. A block_bytes of 0
    // keeps the device's block size; anything else has to be a whole multiple
    // of it. Returns false when the heap is short, an argument is invalid, or
    // the cache is already initialized.
    bool initialize(std::size_t capacity, std::size_t block_bytes = 0);
    // 寫回所有修改過的區塊，然後把緩衝區還給堆積。寫回失敗時快取保持原狀並
    // 回傳那個錯誤：丟掉還沒寫回去的資料不是可以靜默發生的事。
    //
    // Write back every modified block, then give the buffers back to the heap.
    // If a write-back fails the cache stays as it is and returns that error:
    // dropping data that never reached the device must not happen silently.
    Result shutdown();
    bool ready() const { return entries_ != nullptr; }

    std::size_t block_size() const override;
    std::uint64_t block_count() const override;
    Result read_blocks(std::uint64_t first, std::size_t count, void* buffer) override;
    Result write_blocks(std::uint64_t first, std::size_t count, const void* buffer) override;
    // 等同 sync()。 / The same as sync().
    Result flush() override { return sync(); }

    // 取得區塊的緩衝區並把它釘住，直到對應的 release()。所有緩衝區都被釘住、
    // 區塊超出範圍或裝置讀取失敗時回傳 nullptr，原因寫進 error（若不為空）。
    //
    // Take a block's buffer and pin it until the matching release(). Returns
    // nullptr when every buffer is pinned, the block is out of range, or the
    // device read fails, with the reason in error when it is not null.
    std::uint8_t* acquire(std::uint64_t block, Access access, Error* error = nullptr);
    // 放開 acquire() 給出的緩衝區；dirty 表示內容被改過，需要寫回。
    // Let go of a buffer acquire() handed out; dirty says the content changed
    // and has to be written back.
    void release(const void* data, bool dirty);

    // 把所有修改過的區塊寫回裝置。某一塊失敗時其餘的仍會嘗試，並回傳第一個
    // 錯誤；失敗的那塊保持待寫回。
    //
    // Write every modified block back to the device. When one fails the rest
    // are still attempted and the first error is returned; the failed block
    // stays waiting for write-back.
    Result sync();

    std::size_t capacity() const { return capacity_; }
    std::size_t dirty_count() const;
    std::uint64_t hits() const { return hits_; }
    std::uint64_t misses() const { return misses_; }

private:
    struct Entry {
        std::uint64_t block;
        // 最後一次被用到的時間，以快取自己的計數器表示；最小的最先被擠出。
        // When the entry was last used, by the cache's own counter; the
        // smallest is evicted first.
        std::uint64_t last_used;
        std::uint8_t* data;
        std::uint32_t pins;
        bool valid;
        bool dirty;
    };

    Entry* find(std::uint64_t block);
    Entry* find_buffer(const void* data);
    Entry* victim();
    Result write_back(Entry& entry);

    BlockDevice* device_;
    Entry* entries_ = nullptr;
    std::uint8_t* storage_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t block_bytes_ = 0;
    // 一個快取區塊等於幾個裝置區塊。
    // How many device blocks one cached block spans.
    std::size_t device_blocks_ = 0;
    std::uint64_t clock_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
};

} // namespace shirley::io
