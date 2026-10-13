#include "shirley/block_cache.hpp"
#include "shirley/fs.hpp"
#include "shirley/heap.hpp"
#include "shirley/ram_disk.hpp"
#include "shirley/rootfs.hpp"

#include <array>
#include <cassert>
#include <cstring>

namespace {

using namespace shirley;

alignas(4096) unsigned char arena[256 * 1024];

// 記錄每一次真正到達裝置的讀寫，並可以被設定成失敗。
// Counts every read and write that really reaches the device, and can be told
// to fail.
class CountingDevice final : public io::BlockDevice {
public:
    explicit CountingDevice(io::BlockDevice& inner) : inner_(inner) {}
    std::size_t block_size() const override { return inner_.block_size(); }
    std::uint64_t block_count() const override { return inner_.block_count(); }
    io::Result read_blocks(std::uint64_t first, std::size_t count, void* buffer) override {
        if (failing) return {0, io::Error::DeviceError};
        ++reads;
        return inner_.read_blocks(first, count, buffer);
    }
    io::Result write_blocks(std::uint64_t first, std::size_t count, const void* buffer) override {
        if (failing) return {0, io::Error::DeviceError};
        ++writes;
        return inner_.write_blocks(first, count, buffer);
    }
    int reads = 0;
    int writes = 0;
    bool failing = false;

private:
    io::BlockDevice& inner_;
};

std::array<std::uint8_t, 512> filled(std::uint8_t value) {
    std::array<std::uint8_t, 512> block{};
    block.fill(value);
    return block;
}

void test_initialize() {
    std::array<std::uint8_t, 4096> storage{};
    io::RamDisk disk(storage.data(), storage.size());
    io::BlockCache cache(disk);
    assert(!cache.ready() && cache.block_count() == 0);
    std::array<std::uint8_t, 512> block{};
    assert(cache.read_blocks(0, 1, block.data()).error == io::Error::DeviceError);
    assert(!cache.initialize(0));
    // 快取區塊必須是裝置區塊的整數倍。 / A cached block must be a whole
    // multiple of the device block.
    assert(!cache.initialize(4, 768));
    assert(cache.initialize(4));
    assert(!cache.initialize(4));
    assert(cache.ready() && cache.block_size() == 512 && cache.block_count() == 8);
    assert(cache.shutdown());
    assert(!cache.ready());
}

void test_hits_and_write_back() {
    std::array<std::uint8_t, 8 * 512> storage{};
    for (std::size_t i = 0; i < storage.size(); ++i) storage[i] = static_cast<std::uint8_t>(i / 512);
    io::RamDisk disk(storage.data(), storage.size());
    CountingDevice device(disk);
    io::BlockCache cache(device);
    assert(cache.initialize(2));

    std::array<std::uint8_t, 512> block{};
    assert(cache.read_blocks(3, 1, block.data()).transferred == 512 && block[0] == 3);
    assert(cache.read_blocks(3, 1, block.data()) && block[511] == 3);
    assert(device.reads == 1 && cache.hits() == 1 && cache.misses() == 1);

    // 寫入留在快取裡，直到 sync()。 / A write stays in the cache until sync().
    const auto sevens = filled(7);
    assert(cache.write_blocks(5, 1, sevens.data()).transferred == 512);
    assert(device.writes == 0 && storage[5 * 512] == 5 && cache.dirty_count() == 1);
    assert(cache.read_blocks(5, 1, block.data()) && block == sevens);
    assert(cache.sync().transferred == 512);
    assert(device.writes == 1 && storage[5 * 512] == 7 && cache.dirty_count() == 0);
    assert(cache.sync().transferred == 0 && device.writes == 1);

    // 超出範圍的請求在碰到裝置之前就被拒絕。 / An out-of-range request is
    // refused before touching the device.
    assert(cache.read_blocks(7, 2, block.data()).error == io::Error::OutOfRange);
    assert(cache.write_blocks(0, 1, nullptr).error == io::Error::InvalidArgument);
    assert(cache.shutdown());
}

void test_eviction_order() {
    std::array<std::uint8_t, 8 * 512> storage{};
    io::RamDisk disk(storage.data(), storage.size());
    CountingDevice device(disk);
    io::BlockCache cache(device);
    assert(cache.initialize(2));
    std::array<std::uint8_t, 512> block{};

    const auto ones = filled(1);
    assert(cache.write_blocks(0, 1, ones.data()));
    assert(cache.read_blocks(1, 1, block.data()));
    // 再碰一次區塊 0，區塊 1 就變成最久沒用的那一個。
    // Touching block 0 again makes block 1 the least recently used.
    assert(cache.read_blocks(0, 1, block.data()) && block == ones);
    const auto reads = device.reads;
    assert(cache.read_blocks(2, 1, block.data()));
    assert(device.reads == reads + 1 && device.writes == 0);
    // 區塊 0 還在快取裡；區塊 1 被擠出去了。
    // Block 0 is still cached; block 1 was evicted.
    assert(cache.read_blocks(0, 1, block.data()) && device.reads == reads + 1);
    assert(cache.read_blocks(1, 1, block.data()) && device.reads == reads + 2);

    // 擠出修改過的區塊時先寫回。此時區塊 0 是最久沒用的。
    // Evicting a modified block writes it back first. Block 0 is now the least
    // recently used.
    assert(cache.read_blocks(3, 1, block.data()));
    assert(device.writes == 1 && storage[0] == 1);
    assert(cache.shutdown());
}

void test_pinning() {
    std::array<std::uint8_t, 8 * 512> storage{};
    io::RamDisk disk(storage.data(), storage.size());
    io::BlockCache cache(disk);
    assert(cache.initialize(2));

    io::Error error = io::Error::None;
    auto* first = cache.acquire(0, io::BlockCache::Access::Read, &error);
    auto* second = cache.acquire(1, io::BlockCache::Access::Overwrite, &error);
    assert(first != nullptr && second != nullptr && second[0] == 0);
    // 兩個緩衝區都被釘住，第三個區塊沒有地方放。
    // Both buffers are pinned, so a third block has nowhere to go.
    assert(cache.acquire(2, io::BlockCache::Access::Read, &error) == nullptr);
    assert(error == io::Error::DeviceError);
    std::array<std::uint8_t, 512> block{};
    assert(!cache.read_blocks(2, 1, block.data()));

    // 直接修改快取內容，release 時標記為需要寫回。
    // Edit the cached content in place and mark it dirty on release.
    first[10] = 0x5A;
    cache.release(first, true);
    second[0] = 0x6B;
    cache.release(second, false);
    // Overwrite 本身就代表需要寫回。 / Overwrite alone means write-back is due.
    assert(cache.dirty_count() == 2);
    // 讀第三個區塊會擠掉最久沒用的區塊 0，並先把它寫回。
    // Reading a third block evicts block 0, the least recently used, writing it
    // back first.
    assert(cache.read_blocks(2, 1, block.data()));
    assert(storage[10] == 0x5A && storage[512] == 0);
    assert(cache.sync());
    assert(storage[10] == 0x5A && storage[512] == 0x6B);
    assert(cache.acquire(8, io::BlockCache::Access::Read, &error) == nullptr);
    assert(error == io::Error::OutOfRange);
    assert(cache.shutdown());
}

void test_failures_keep_data() {
    std::array<std::uint8_t, 8 * 512> storage{};
    io::RamDisk disk(storage.data(), storage.size());
    CountingDevice device(disk);
    io::BlockCache cache(device);
    assert(cache.initialize(1));
    const auto nines = filled(9);
    assert(cache.write_blocks(4, 1, nines.data()));

    device.failing = true;
    std::array<std::uint8_t, 512> block{};
    // 寫回失敗：讀另一個區塊失敗，修改過的區塊仍在快取裡。
    // The write-back fails: reading another block fails, and the modified block
    // stays in the cache.
    assert(cache.read_blocks(0, 1, block.data()).error == io::Error::DeviceError);
    assert(cache.dirty_count() == 1);
    assert(cache.sync().error == io::Error::DeviceError);
    assert(cache.shutdown().error == io::Error::DeviceError && cache.ready());

    device.failing = false;
    assert(cache.shutdown() && !cache.ready());
    assert(storage[4 * 512] == 9);
}

void test_larger_blocks() {
    std::array<std::uint8_t, 16 * 512> storage{};
    for (std::size_t i = 0; i < storage.size(); ++i) storage[i] = static_cast<std::uint8_t>(i / 512);
    io::RamDisk disk(storage.data(), storage.size());
    CountingDevice device(disk);
    io::BlockCache cache(device);
    // 4 KiB 的快取區塊架在 512 位元組的裝置上。 / 4 KiB cached blocks over a
    // 512-byte device.
    assert(cache.initialize(2, 4096));
    assert(cache.block_size() == 4096 && cache.block_count() == 2);
    std::array<std::uint8_t, 4096> block{};
    assert(cache.read_blocks(1, 1, block.data()).transferred == 4096);
    assert(block[0] == 8 && block[4095] == 15 && device.reads == 1);
    assert(cache.shutdown());
}

// 透過快取掛載根檔案系統，看到的必須和直接掛在 RAM disk 上完全一樣。
// The root file system mounted through the cache must look exactly as it does
// mounted straight on the RAM disk.
void test_shrfs_through_cache() {
    io::RamDisk disk(fs::rootfs_image(), fs::rootfs_image_size());
    io::BlockCache cache(disk);
    assert(cache.initialize(8));
    assert(fs::mount(cache));
    fs::Node motd;
    assert(fs::lookup("/etc/motd", motd));
    char cached[1024]{};
    const auto cached_bytes = fs::read(motd, 0, cached, sizeof(cached) - 1);
    assert(cached_bytes && cached_bytes.transferred == motd.size);
    const auto entries = fs::entry_count();
    fs::unmount();

    assert(fs::mount(disk));
    assert(fs::entry_count() == entries);
    assert(fs::lookup("/etc/motd", motd));
    char direct[1024]{};
    assert(fs::read(motd, 0, direct, sizeof(direct) - 1));
    assert(std::strcmp(cached, direct) == 0);
    fs::unmount();
    assert(cache.hits() > 0);
    assert(cache.shutdown());
}

} // namespace

int main() {
    heap::initialize(arena, sizeof(arena));
    test_initialize();
    test_hits_and_write_back();
    test_eviction_order();
    test_pinning();
    test_failures_keep_data();
    test_larger_blocks();
    test_shrfs_through_cache();
    // 每個快取都關掉了，堆積必須完整歸還。
    // Every cache was shut down, so the heap must be whole again.
    assert(heap::available() == heap::capacity() && heap::consistent());
    return 0;
}
