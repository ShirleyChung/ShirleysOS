#include "shirley/block_cache.hpp"

#include "shirley/heap.hpp"

#include <limits>

namespace shirley::io {
namespace {

void copy_bytes(void* destination, const void* source, std::size_t count) {
    auto* out = static_cast<std::uint8_t*>(destination);
    const auto* in = static_cast<const std::uint8_t*>(source);
    for (std::size_t i = 0; i < count; ++i) out[i] = in[i];
}

void zero_bytes(void* destination, std::size_t count) {
    auto* out = static_cast<std::uint8_t*>(destination);
    for (std::size_t i = 0; i < count; ++i) out[i] = 0;
}

// 緩衝區的對齊：區塊大小是 2 的次方時對齊到區塊大小（最多一頁），之後的
// DMA 驅動程式拿到的緩衝區就不會跨頁；否則用堆積的預設對齊。
//
// The buffers' alignment: the block size when it is a power of two, capped at
// a page, so a later DMA driver never gets a buffer straddling a page;
// otherwise the heap's default.
std::size_t buffer_alignment(std::size_t block_bytes) {
    if ((block_bytes & (block_bytes - 1)) != 0) return heap::default_alignment;
    return block_bytes < 4096 ? block_bytes : 4096;
}

} // namespace

bool BlockCache::initialize(std::size_t capacity, std::size_t block_bytes) {
    if (ready() || capacity == 0) return false;
    const auto device_bytes = device_->block_size();
    if (device_bytes == 0) return false;
    if (block_bytes == 0) block_bytes = device_bytes;
    if (block_bytes % device_bytes != 0) return false;
    if (capacity > std::numeric_limits<std::size_t>::max() / block_bytes ||
        capacity > std::numeric_limits<std::size_t>::max() / sizeof(Entry))
        return false;

    auto* entries = static_cast<Entry*>(heap::allocate(capacity * sizeof(Entry)));
    auto* storage = static_cast<std::uint8_t*>(
        heap::allocate(capacity * block_bytes, buffer_alignment(block_bytes)));
    if (entries == nullptr || storage == nullptr) {
        heap::free(entries);
        heap::free(storage);
        return false;
    }
    for (std::size_t i = 0; i < capacity; ++i)
        entries[i] = Entry{0, 0, storage + i * block_bytes, 0, false, false};

    entries_ = entries;
    storage_ = storage;
    capacity_ = capacity;
    block_bytes_ = block_bytes;
    device_blocks_ = block_bytes / device_bytes;
    clock_ = hits_ = misses_ = 0;
    return true;
}

Result BlockCache::shutdown() {
    if (!ready()) return {0, Error::None};
    const auto flushed = sync();
    if (!flushed) return flushed;
    heap::free(entries_);
    heap::free(storage_);
    entries_ = nullptr;
    storage_ = nullptr;
    capacity_ = block_bytes_ = device_blocks_ = 0;
    return flushed;
}

std::size_t BlockCache::block_size() const { return block_bytes_; }

std::uint64_t BlockCache::block_count() const {
    return ready() ? device_->block_count() / device_blocks_ : 0;
}

Result BlockCache::read_blocks(std::uint64_t first, std::size_t count, void* buffer) {
    if (count != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    if (!ready()) return {0, Error::DeviceError};
    if (first > block_count() || count > block_count() - first ||
        count > std::numeric_limits<std::size_t>::max() / block_bytes_)
        return {0, Error::OutOfRange};
    auto* out = static_cast<std::uint8_t*>(buffer);
    for (std::size_t i = 0; i < count; ++i) {
        Error error = Error::None;
        const auto* data = acquire(first + i, Access::Read, &error);
        if (data == nullptr) return {i * block_bytes_, error};
        copy_bytes(out + i * block_bytes_, data, block_bytes_);
        release(data, false);
    }
    return {count * block_bytes_, Error::None};
}

Result BlockCache::write_blocks(std::uint64_t first, std::size_t count, const void* buffer) {
    if (count != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    if (!ready()) return {0, Error::DeviceError};
    if (first > block_count() || count > block_count() - first ||
        count > std::numeric_limits<std::size_t>::max() / block_bytes_)
        return {0, Error::OutOfRange};
    const auto* in = static_cast<const std::uint8_t*>(buffer);
    for (std::size_t i = 0; i < count; ++i) {
        Error error = Error::None;
        auto* data = acquire(first + i, Access::Overwrite, &error);
        if (data == nullptr) return {i * block_bytes_, error};
        copy_bytes(data, in + i * block_bytes_, block_bytes_);
        release(data, true);
    }
    return {count * block_bytes_, Error::None};
}

std::uint8_t* BlockCache::acquire(std::uint64_t block, Access access, Error* error) {
    const auto fail = [error](Error reason) -> std::uint8_t* {
        if (error != nullptr) *error = reason;
        return nullptr;
    };
    if (!ready()) return fail(Error::DeviceError);
    if (block >= block_count()) return fail(Error::OutOfRange);

    Entry* entry = find(block);
    if (entry != nullptr) {
        ++hits_;
    } else {
        ++misses_;
        entry = victim();
        // 所有緩衝區都被釘住：呼叫端同時抓著的區塊比快取還多。
        // Every buffer is pinned: callers are holding more blocks at once than
        // the cache has.
        if (entry == nullptr) return fail(Error::DeviceError);
        if (entry->valid && entry->dirty) {
            const auto written = write_back(*entry);
            if (!written) return fail(written.error);
        }
        entry->valid = false;
        entry->block = block;
        if (access == Access::Read) {
            const auto loaded =
                device_->read_blocks(block * device_blocks_, device_blocks_, entry->data);
            if (!loaded) return fail(loaded.error);
        }
        entry->valid = true;
        entry->dirty = false;
    }
    if (access == Access::Overwrite) {
        // 呼叫端宣告會覆寫整塊，因此它現在就是需要寫回的內容；清成零讓沒寫到的
        // 部分是確定的值，而不是上一個區塊的殘留。
        //
        // The caller declared it will replace the whole block, so it is content
        // awaiting write-back from now on. Zeroing makes any part left unwritten
        // a known value rather than what the previous block left behind.
        zero_bytes(entry->data, block_bytes_);
        entry->dirty = true;
    }
    ++entry->pins;
    entry->last_used = ++clock_;
    return entry->data;
}

void BlockCache::release(const void* data, bool dirty) {
    Entry* entry = find_buffer(data);
    if (entry == nullptr || entry->pins == 0) return;
    --entry->pins;
    if (dirty) entry->dirty = true;
}

Result BlockCache::sync() {
    Result first_failure{0, Error::None};
    std::size_t written = 0;
    for (std::size_t i = 0; i < capacity_; ++i) {
        Entry& entry = entries_[i];
        if (!entry.valid || !entry.dirty) continue;
        const auto result = write_back(entry);
        if (result) {
            written += block_bytes_;
        } else if (first_failure.error == Error::None) {
            first_failure.error = result.error;
        }
    }
    first_failure.transferred = written;
    return first_failure;
}

std::size_t BlockCache::dirty_count() const {
    std::size_t count = 0;
    for (std::size_t i = 0; i < capacity_; ++i)
        if (entries_[i].valid && entries_[i].dirty) ++count;
    return count;
}

// 線性搜尋。快取只有幾百個項目，雜湊表要等量測說它值得再加。
// A linear search. The cache holds a few hundred entries at most; a hash table
// waits until a measurement says it is worth having.
BlockCache::Entry* BlockCache::find(std::uint64_t block) {
    for (std::size_t i = 0; i < capacity_; ++i)
        if (entries_[i].valid && entries_[i].block == block) return &entries_[i];
    return nullptr;
}

BlockCache::Entry* BlockCache::find_buffer(const void* data) {
    for (std::size_t i = 0; i < capacity_; ++i)
        if (entries_[i].data == data) return &entries_[i];
    return nullptr;
}

// 優先用空的項目，其次是最久沒用到、而且沒被釘住的那一個。
// An empty entry first, otherwise the unpinned one unused the longest.
BlockCache::Entry* BlockCache::victim() {
    Entry* oldest = nullptr;
    for (std::size_t i = 0; i < capacity_; ++i) {
        Entry& entry = entries_[i];
        if (entry.pins != 0) continue;
        if (!entry.valid) return &entry;
        if (oldest == nullptr || entry.last_used < oldest->last_used) oldest = &entry;
    }
    return oldest;
}

Result BlockCache::write_back(Entry& entry) {
    const auto result =
        device_->write_blocks(entry.block * device_blocks_, device_blocks_, entry.data);
    if (result) entry.dirty = false;
    return result;
}

} // namespace shirley::io
