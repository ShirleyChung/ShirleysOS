#pragma once

#include <cstddef>
#include <cstdint>

// virtio：QEMU 這類虛擬機器提供的半虛擬化裝置。
//
// 一個 virtio 裝置有兩半：怎麼摸到它的暫存器（transport），以及跟它說話的
// 協定（split virtqueue 與裝置類型自己的請求格式）。前者是硬體細節，放在
// platform/——x86 PC 上是 PCI 與 port I/O，ARM 的 QEMU virt 上是一排 MMIO
// 插槽；後者與硬體無關，放在 drivers/virtio/，主機測試也編得到。
//
// 目前實作的是 legacy（0.9.5）介面：QEMU 預設就提供它，而且 PCI 上它走 I/O
// port，不需要映射 1 GiB identity map 之外的 BAR。
//
// virtio: the paravirtualized devices that virtual machines such as QEMU
// provide.
//
// A virtio device has two halves: how its registers are reached (the
// transport), and the protocol spoken to it (the split virtqueue plus the
// request format of its device type). The first is hardware detail and lives
// in platform/ — PCI and port I/O on an x86 PC, a row of MMIO slots on ARM's
// QEMU virt. The second is hardware-independent and lives in drivers/virtio/,
// where the host tests can reach it.
//
// What is implemented is the legacy (0.9.5) interface: QEMU offers it by
// default, and on PCI it goes through I/O ports, so no BAR outside the 1 GiB
// identity map has to be mapped.
namespace shirley::virtio {

// 裝置狀態位元。 / Device status bits.
constexpr std::uint8_t status_acknowledge = 1;
constexpr std::uint8_t status_driver = 2;
constexpr std::uint8_t status_driver_ok = 4;
constexpr std::uint8_t status_failed = 128;

// 裝置類型編號。 / Device type numbers.
constexpr std::uint32_t device_block = 2;

// virtqueue 的記憶體配置以這個大小對齊（legacy 介面固定為一頁）。
// The alignment of a virtqueue's memory layout (one page in the legacy
// interface).
constexpr std::size_t queue_alignment = 4096;

// 一個裝置的暫存器存取。實作必須在 notify() 之前讓先前對記憶體的寫入對裝置
// 可見（ARM 上就是一道 dsb）。
//
// Register access to one device. An implementation has to make earlier memory
// writes visible to the device before notify() — a dsb on ARM.
class Transport {
public:
    virtual std::uint32_t device_features() = 0;
    virtual void set_driver_features(std::uint32_t features) = 0;
    virtual std::uint8_t status() = 0;
    virtual void set_status(std::uint8_t status) = 0;
    // 選取一個佇列並回報它最大能有多少項目；0 表示沒有這個佇列。
    // Select a queue and report how many entries it can hold at most; 0 means
    // there is no such queue.
    virtual std::uint16_t queue_max(std::uint16_t queue) = 0;
    // legacy PCI 的佇列大小由裝置決定，驅動程式只能照單全收；MMIO 可以選小一點。
    // A legacy PCI queue's size is the device's to choose and the driver must
    // take it; MMIO lets the driver pick something smaller.
    virtual bool queue_size_fixed() const = 0;
    // 告訴裝置佇列在哪裡。physical 是對齊 queue_alignment 的實體位址。
    // Tell the device where the queue is. physical is a physical address
    // aligned to queue_alignment.
    virtual bool set_queue(std::uint16_t queue, std::uint16_t size, std::uint64_t physical) = 0;
    virtual void notify(std::uint16_t queue) = 0;
    // 裝置類型自己的設定空間，以 32 位元讀取。
    // The device type's own configuration space, read 32 bits at a time.
    virtual std::uint32_t config32(std::size_t offset) = 0;

protected:
    ~Transport() = default;
};

// 把 transport 後面的 virtio-blk 裝置啟動起來，以 vda、vdb……的名字登記到
// 裝置註冊表，並發布它的分割區。需要堆積：佇列與跳板緩衝區從堆積配置。裝置
// 拒絕、佇列不存在、堆積不夠或表已滿時回傳 false。
//
// Bring up the virtio-blk device behind transport, register it in the device
// registry as vda, vdb and so on, and publish its partitions. It needs the
// heap: the queue and the bounce buffer come from it. Returns false when the
// device refuses, has no queue, the heap is short, or the table is full.
bool attach_block(Transport& transport);

// 同時可以存在的 virtio-blk 裝置數。 / How many virtio-blk devices can exist
// at once.
constexpr std::size_t max_block_devices = 4;
std::size_t block_device_count();

} // namespace shirley::virtio
