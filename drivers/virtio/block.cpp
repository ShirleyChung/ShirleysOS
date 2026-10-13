#include "shirley/virtio.hpp"

#include "shirley/block_device.hpp"
#include "shirley/console.hpp"
#include "shirley/device.hpp"
#include "shirley/format.hpp"
#include "shirley/heap.hpp"
#include "shirley/partition.hpp"
#include "shirley/text.hpp"

#include <limits>

namespace shirley::virtio {
namespace {

// split virtqueue 的三個部分。legacy 介面把它們排在一段連續記憶體裡：描述子
// 表，緊接著 available ring，然後對齊到下一頁放 used ring。
//
// The three parts of a split virtqueue. The legacy interface lays them out in
// one run of memory: the descriptor table, the available ring right after it,
// then the used ring at the next page boundary.
struct Descriptor {
    std::uint64_t address;
    std::uint32_t length;
    std::uint16_t flags;
    std::uint16_t next;
};
static_assert(sizeof(Descriptor) == 16);

constexpr std::uint16_t descriptor_next = 1;
constexpr std::uint16_t descriptor_write = 2;
// 請裝置不要在處理完請求時發中斷：驅動程式是輪詢的。
// Ask the device not to interrupt when it finishes a request: the driver polls.
constexpr std::uint16_t available_no_interrupt = 1;

// virtio-blk 的請求種類與功能位元。 / virtio-blk request types and feature
// bits.
constexpr std::uint32_t request_read = 0;
constexpr std::uint32_t request_write = 1;
constexpr std::uint32_t feature_read_only = 1u << 5;
constexpr std::uint8_t request_ok = 0;
// virtio-blk 永遠以 512 位元組的磁區定址。 / virtio-blk always addresses
// 512-byte sectors.
constexpr std::size_t sector_bytes = 512;
// 跳板緩衝區的大小，也是單一請求最多搬的資料量。
// The bounce buffer's size, which is also the most one request moves.
constexpr std::size_t bounce_sectors = 32;
// MMIO 讓驅動程式選佇列大小。一次只有一個請求、每個請求三個描述子，8 個就夠。
// MMIO lets the driver choose the queue size. One request at a time, three
// descriptors each, so 8 is plenty.
constexpr std::uint16_t preferred_queue_size = 8;
// 輪詢的上限。超過就當成裝置壞了：請求仍在裝置手上，記憶體不能再交給下一個
// 請求，因此之後的存取一律失敗。
//
// The polling bound. Past it the device is treated as broken: the request is
// still in the device's hands, its memory cannot go to another request, so
// every later access fails.
constexpr std::uint64_t poll_limit = 1ull << 28;

struct RequestHeader {
    std::uint32_t type;
    std::uint32_t reserved;
    std::uint64_t sector;
};

constexpr std::size_t align_up(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// 核心的記憶體是 identity map 的，堆積在核心映像之內，因此虛擬位址就是實體
// 位址。這是 DMA 只用堆積記憶體、從不碰呼叫端緩衝區的原因之一：系統呼叫傳進來
// 的可能是 user 的虛擬位址。
//
// Kernel memory is identity-mapped and the heap lies inside the kernel image,
// so a virtual address is the physical one. That is one reason DMA only ever
// touches heap memory and never the caller's buffer: a buffer passed in by a
// syscall may be a user virtual address.
std::uint64_t physical(const void* pointer) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(pointer));
}

void copy_bytes(void* destination, const void* source, std::size_t count) {
    auto* out = static_cast<std::uint8_t*>(destination);
    const auto* in = static_cast<const std::uint8_t*>(source);
    for (std::size_t i = 0; i < count; ++i) out[i] = in[i];
}

class BlockDisk final : public io::BlockDevice {
public:
    constexpr BlockDisk() = default;
    bool start(Transport& transport);

    std::size_t block_size() const override { return sector_bytes; }
    std::uint64_t block_count() const override { return sectors_; }
    io::Result read_blocks(std::uint64_t first, std::size_t count, void* buffer) override;
    io::Result write_blocks(std::uint64_t first, std::size_t count, const void* buffer) override;

private:
    io::Result transfer(std::uint32_t type, std::uint64_t first, std::size_t count);
    io::Result validate(std::uint64_t first, std::size_t count, const void* buffer) const;

    Transport* transport_ = nullptr;
    std::uint16_t queue_size_ = 0;
    Descriptor* descriptors_ = nullptr;
    volatile std::uint16_t* available_ = nullptr;
    volatile std::uint16_t* used_ = nullptr;
    // 請求標頭、狀態位元組與資料跳板都在這一塊堆積記憶體裡。
    // The request header, the status byte and the data bounce buffer all live
    // in this one piece of heap memory.
    RequestHeader* header_ = nullptr;
    volatile std::uint8_t* status_ = nullptr;
    std::uint8_t* bounce_ = nullptr;
    std::uint16_t last_used_ = 0;
    std::uint64_t sectors_ = 0;
    bool read_only_ = false;
    bool broken_ = false;
};

bool BlockDisk::start(Transport& transport) {
    transport.set_status(0);
    transport.set_status(status_acknowledge);
    transport.set_status(status_acknowledge | status_driver);
    // 不要求任何功能；唯讀位元只拿來看，不需要接受。
    // Ask for no features; the read-only bit is only looked at and need not be
    // accepted.
    read_only_ = (transport.device_features() & feature_read_only) != 0;
    transport.set_driver_features(0);

    const auto maximum = transport.queue_max(0);
    if (maximum == 0) {
        transport.set_status(status_failed);
        return false;
    }
    queue_size_ = transport.queue_size_fixed() || maximum < preferred_queue_size ? maximum
                                                                                   : preferred_queue_size;
    const std::size_t available_bytes = 6 + 2 * static_cast<std::size_t>(queue_size_);
    const std::size_t used_offset =
        align_up(16 * static_cast<std::size_t>(queue_size_) + available_bytes, queue_alignment);
    const std::size_t used_bytes = 6 + 8 * static_cast<std::size_t>(queue_size_);
    const std::size_t ring_bytes = used_offset + align_up(used_bytes, queue_alignment);
    const std::size_t data_bytes = sizeof(RequestHeader) + 16 + bounce_sectors * sector_bytes;

    auto* ring = static_cast<std::uint8_t*>(heap::allocate(ring_bytes, queue_alignment));
    auto* data = static_cast<std::uint8_t*>(heap::allocate(data_bytes, 64));
    if (ring == nullptr || data == nullptr) {
        heap::free(ring);
        heap::free(data);
        transport.set_status(status_failed);
        return false;
    }
    for (std::size_t i = 0; i < ring_bytes; ++i) ring[i] = 0;
    descriptors_ = reinterpret_cast<Descriptor*>(ring);
    available_ = reinterpret_cast<volatile std::uint16_t*>(ring + 16 * queue_size_);
    used_ = reinterpret_cast<volatile std::uint16_t*>(ring + used_offset);
    header_ = reinterpret_cast<RequestHeader*>(data);
    status_ = data + sizeof(RequestHeader);
    bounce_ = data + sizeof(RequestHeader) + 16;
    available_[0] = available_no_interrupt;

    if (!transport.set_queue(0, queue_size_, physical(ring))) {
        heap::free(ring);
        heap::free(data);
        transport.set_status(status_failed);
        return false;
    }
    // 容量以 512 位元組的磁區計，放在設定空間的開頭。
    // The capacity, in 512-byte sectors, opens the configuration space.
    sectors_ = transport.config32(0) | (static_cast<std::uint64_t>(transport.config32(4)) << 32);
    transport_ = &transport;
    transport.set_status(status_acknowledge | status_driver | status_driver_ok);
    return true;
}

io::Result BlockDisk::validate(std::uint64_t first, std::size_t count, const void* buffer) const {
    if (count != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (broken_ || transport_ == nullptr) return {0, io::Error::DeviceError};
    if (first > sectors_ || count > sectors_ - first ||
        count > std::numeric_limits<std::size_t>::max() / sector_bytes)
        return {0, io::Error::OutOfRange};
    return {0, io::Error::None};
}

// 送出一個請求並等它完成：標頭、資料、狀態三個描述子串成一條，放進 available
// ring，通知裝置，然後輪詢 used ring。一次只有一個請求在飛，因此永遠用描述子
// 0、1、2。
//
// Send one request and wait for it: header, data and status descriptors
// chained together, placed in the available ring, the device notified, the
// used ring polled. Only one request is ever in flight, so descriptors 0, 1 and
// 2 are always the ones used.
io::Result BlockDisk::transfer(std::uint32_t type, std::uint64_t first, std::size_t count) {
    header_->type = type;
    header_->reserved = 0;
    header_->sector = first;
    *status_ = 0xFF;
    const auto bytes = static_cast<std::uint32_t>(count * sector_bytes);
    descriptors_[0] = {physical(header_), sizeof(RequestHeader), descriptor_next, 1};
    descriptors_[1] = {physical(bounce_), bytes,
                       static_cast<std::uint16_t>(descriptor_next |
                                                  (type == request_read ? descriptor_write : 0)),
                       2};
    descriptors_[2] = {physical(const_cast<std::uint8_t*>(status_)), 1, descriptor_write, 0};

    const std::uint16_t index = available_[1];
    available_[2 + index % queue_size_] = 0;
    // 描述子與 ring 項目必須在 idx 前進之前寫好，idx 必須在通知之前寫好。
    // The descriptors and ring slot must be written before idx advances, and
    // idx before the notification.
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    available_[1] = static_cast<std::uint16_t>(index + 1);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    transport_->notify(0);

    std::uint64_t polls = 0;
    while (used_[1] == last_used_) {
        if (++polls == poll_limit) {
            broken_ = true;
            console::write("[virtio] block request timed out; device disabled\n");
            return {0, io::Error::DeviceError};
        }
    }
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    ++last_used_;
    if (*status_ != request_ok) return {0, io::Error::DeviceError};
    return {bytes, io::Error::None};
}

io::Result BlockDisk::read_blocks(std::uint64_t first, std::size_t count, void* buffer) {
    const auto checked = validate(first, count, buffer);
    if (!checked) return checked;
    auto* out = static_cast<std::uint8_t*>(buffer);
    std::size_t done = 0;
    while (done < count) {
        const auto chunk = count - done < bounce_sectors ? count - done : bounce_sectors;
        const auto result = transfer(request_read, first + done, chunk);
        if (!result) return {done * sector_bytes, result.error};
        copy_bytes(out + done * sector_bytes, bounce_, chunk * sector_bytes);
        done += chunk;
    }
    return {count * sector_bytes, io::Error::None};
}

io::Result BlockDisk::write_blocks(std::uint64_t first, std::size_t count, const void* buffer) {
    const auto checked = validate(first, count, buffer);
    if (!checked) return checked;
    if (read_only_) return {0, io::Error::Unsupported};
    const auto* in = static_cast<const std::uint8_t*>(buffer);
    std::size_t done = 0;
    while (done < count) {
        const auto chunk = count - done < bounce_sectors ? count - done : bounce_sectors;
        copy_bytes(bounce_, in + done * sector_bytes, chunk * sector_bytes);
        const auto result = transfer(request_write, first + done, chunk);
        if (!result) return {done * sector_bytes, result.error};
        done += chunk;
    }
    return {count * sector_bytes, io::Error::None};
}

struct Slot {
    BlockDisk disk;
    device::Device device;
};

// constinit：核心不執行全域建構子。 / constinit: the kernel runs no global
// constructors.
constinit Slot slots[max_block_devices]{};
std::size_t attached = 0;

void report(const char* name, std::uint64_t sectors, std::size_t partitions) {
    char digits[21];
    console::write("[virtio] ");
    console::write(name);
    console::write(": ");
    format::to_decimal(digits, sizeof(digits), sectors * sector_bytes / (1024 * 1024));
    console::write(digits);
    console::write(" MiB, ");
    format::to_decimal(digits, sizeof(digits), partitions);
    console::write(digits);
    console::write(partitions == 1 ? " partition\n" : " partitions\n");
}

} // namespace

bool attach_block(Transport& transport) {
    if (attached == max_block_devices) return false;
    Slot& slot = slots[attached];
    if (!slot.disk.start(transport)) return false;
    const char name[] = {'v', 'd', static_cast<char>('a' + attached), '\0'};
    slot.device = device::Device(name, device::Type::Block, device::block_operations, &slot.disk);
    if (device::register_device(slot.device) != device::Status::Ok) return false;
    ++attached;
    report(name, slot.disk.block_count(), partition::publish(slot.device));
    return true;
}

std::size_t block_device_count() { return attached; }

} // namespace shirley::virtio
