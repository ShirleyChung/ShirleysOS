#include "shirley/device.hpp"
#include "shirley/heap.hpp"
#include "shirley/virtio.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

// 一個在主機上扮演 virtio-blk 裝置的 transport：notify() 時照規格走 split
// virtqueue，把請求套用到一塊記憶體上。主機上虛擬位址就是「實體」位址，和核心
// 的 identity map 一樣。真正的裝置由 QEMU 的開機測試涵蓋。
//
// A transport that plays a virtio-blk device on the host: on notify() it walks
// the split virtqueue as the specification lays it out and applies the request
// to a block of memory. On the host a virtual address is the "physical" one,
// just as under the kernel's identity map. The real device is covered by the
// QEMU boot tests.

namespace {

using namespace shirley;

alignas(4096) unsigned char arena[256 * 1024];

class FakeBlock final : public virtio::Transport {
public:
    FakeBlock(std::size_t sectors, std::uint16_t queue, bool fixed)
        : disk(sectors * 512), maximum(queue), fixed(fixed) {}

    std::uint32_t device_features() override { return features; }
    void set_driver_features(std::uint32_t value) override { accepted = value; }
    std::uint8_t status() override { return state; }
    void set_status(std::uint8_t value) override {
        state = value;
        statuses.push_back(value);
    }
    std::uint16_t queue_max(std::uint16_t queue) override { return queue == 0 ? maximum : 0; }
    bool queue_size_fixed() const override { return fixed; }
    bool set_queue(std::uint16_t queue, std::uint16_t count, std::uint64_t physical) override {
        assert(queue == 0 && physical % virtio::queue_alignment == 0);
        size = count;
        ring = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(physical));
        return true;
    }
    void notify(std::uint16_t queue) override {
        assert(queue == 0 && (state & virtio::status_driver_ok) != 0);
        const auto* available = reinterpret_cast<const std::uint16_t*>(ring + 16 * size);
        // 驅動程式要求不要中斷。 / The driver asked for no interrupts.
        assert(available[0] == 1);
        const std::size_t used_offset = (16 * size + 6 + 2 * size + 4095) & ~std::size_t{4095};
        auto* used = reinterpret_cast<std::uint16_t*>(ring + used_offset);
        while (seen != available[1]) {
            const auto head = available[2 + seen % size];
            serve(head);
            auto* element = reinterpret_cast<std::uint32_t*>(ring + used_offset + 4 + 8 * (used[1] % size));
            element[0] = head;
            element[1] = 0;
            ++used[1];
            ++seen;
        }
    }
    std::uint32_t config32(std::size_t offset) override {
        const std::uint64_t capacity = disk.size() / 512;
        return offset == 0 ? static_cast<std::uint32_t>(capacity)
                           : static_cast<std::uint32_t>(capacity >> 32);
    }

    struct Descriptor {
        std::uint64_t address;
        std::uint32_t length;
        std::uint16_t flags;
        std::uint16_t next;
    };

    void serve(std::uint16_t head) {
        const auto* table = reinterpret_cast<const Descriptor*>(ring);
        const auto& header = table[head];
        assert((header.flags & 1) != 0 && (header.flags & 2) == 0 && header.length == 16);
        const auto& data = table[header.next];
        const auto& status = table[data.next];
        assert((data.flags & 1) != 0 && (status.flags & 2) != 0 && status.length == 1);
        const auto* request = reinterpret_cast<const std::uint8_t*>(header.address);
        std::uint32_t type;
        std::uint64_t sector;
        std::memcpy(&type, request, 4);
        std::memcpy(&sector, request + 8, 8);
        auto* buffer = reinterpret_cast<std::uint8_t*>(data.address);
        auto* result = reinterpret_cast<std::uint8_t*>(status.address);
        ++requests;
        if (sector * 512 + data.length > disk.size() || failing) {
            *result = 1;
            return;
        }
        if (type == 0) {
            // 讀取時資料描述子必須是裝置可寫的。 / For a read the data
            // descriptor has to be device-writable.
            assert((data.flags & 2) != 0);
            std::memcpy(buffer, disk.data() + sector * 512, data.length);
        } else {
            assert(type == 1 && (data.flags & 2) == 0);
            std::memcpy(disk.data() + sector * 512, buffer, data.length);
        }
        *result = 0;
    }

    std::vector<std::uint8_t> disk;
    std::uint16_t maximum;
    bool fixed;
    std::uint32_t features = 0;
    std::uint32_t accepted = 0xFFFFFFFF;
    std::uint8_t state = 0xFF;
    std::vector<std::uint8_t> statuses;
    std::uint16_t size = 0;
    std::uint8_t* ring = nullptr;
    std::uint16_t seen = 0;
    int requests = 0;
    bool failing = false;
};

void test_bring_up_and_io() {
    // legacy PCI：佇列大小由裝置決定。 / Legacy PCI: the device fixes the
    // queue size.
    FakeBlock pci(2048, 256, true);
    for (std::size_t i = 0; i < pci.disk.size(); ++i) pci.disk[i] = static_cast<std::uint8_t>(i / 512);
    assert(virtio::attach_block(pci));
    assert(pci.size == 256 && pci.accepted == 0);
    // 重設、ACKNOWLEDGE、DRIVER、DRIVER_OK，依序。 / Reset, ACKNOWLEDGE,
    // DRIVER, DRIVER_OK, in that order.
    assert((pci.statuses == std::vector<std::uint8_t>{0, 1, 3, 7}));

    auto* vda = device::find("vda");
    assert(vda != nullptr && vda->block_size() == 512 && vda->block_count() == 2048);
    std::vector<std::uint8_t> buffer(100 * 512);
    // 100 個磁區要分成好幾個請求，每個最多 32 個。掛上時讀分割表已經送過請求，
    // 所以從現在的數字開始算。
    // 100 sectors take several requests of at most 32 each. Reading the
    // partition table at attach time already sent requests, so count from here.
    const auto before = pci.requests;
    const auto read = vda->block_read(10, 100, buffer.data());
    assert(read && read.transferred == buffer.size());
    for (std::size_t i = 0; i < 100; ++i) assert(buffer[i * 512] == static_cast<std::uint8_t>(10 + i));
    assert(pci.requests - before == 4);

    std::memset(buffer.data(), 0x7E, 512);
    assert(vda->block_write(2047, 1, buffer.data()));
    assert(pci.disk[2047 * 512] == 0x7E);
    assert(vda->block_read(2047, 2, buffer.data()).error == io::Error::OutOfRange);

    // 裝置回報錯誤。 / The device reports an error.
    pci.failing = true;
    assert(vda->block_read(0, 1, buffer.data()).error == io::Error::DeviceError);
    pci.failing = false;
    assert(vda->block_read(0, 1, buffer.data()));
}

void test_mmio_queue_and_read_only() {
    // MMIO：驅動程式自己選一個小佇列；唯讀的裝置拒絕寫入。
    // MMIO: the driver picks a small queue itself; a read-only device refuses
    // writes.
    FakeBlock mmio(64, 1024, false);
    mmio.features = 1u << 5;
    assert(virtio::attach_block(mmio));
    assert(mmio.size == 8);
    auto* vdb = device::find("vdb");
    assert(vdb != nullptr);
    std::uint8_t block[512]{};
    assert(vdb->block_read(63, 1, block));
    assert(vdb->block_write(0, 1, block).error == io::Error::Unsupported);
    // 佇列只有 8 項，連續好幾個請求要能繞回 ring 的開頭。
    // The queue holds only 8 entries, so a run of requests has to wrap around
    // the ring.
    for (int i = 0; i < 20; ++i) assert(vdb->block_read(i, 1, block));
}

void test_partitions_are_published() {
    FakeBlock partitioned(4096, 128, true);
    auto& disk = partitioned.disk;
    disk[446 + 4] = 0x0C;
    const std::uint32_t start = 2048, count = 1024;
    std::memcpy(&disk[446 + 8], &start, 4);
    std::memcpy(&disk[446 + 12], &count, 4);
    disk[510] = 0x55;
    disk[511] = 0xAA;
    assert(virtio::attach_block(partitioned));
    auto* partition = device::find("vdc1");
    assert(partition != nullptr && partition->block_count() == 1024);
}

void test_refusals() {
    FakeBlock missing(64, 0, true);
    assert(!virtio::attach_block(missing));
    assert(missing.statuses.back() == virtio::status_failed);
    assert(virtio::block_device_count() == 3);
}

} // namespace

int main() {
    heap::initialize(arena, sizeof(arena));
    device::initialize();
    test_bring_up_and_io();
    test_mmio_queue_and_read_only();
    test_partitions_are_published();
    test_refusals();
    assert(heap::consistent());
    return 0;
}
