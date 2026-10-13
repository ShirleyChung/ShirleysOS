#include "shirley/platform/arm/virtio_mmio.hpp"

#include "shirley/console.hpp"
#include "shirley/virtio.hpp"

namespace shirley::platform::arm {
namespace {

constexpr std::uint32_t magic = 0x74726976; // "virt"

// legacy（版本 1）MMIO 暫存器的位置。 / The legacy (version 1) MMIO register
// offsets.
constexpr std::size_t register_magic = 0x000;
constexpr std::size_t register_version = 0x004;
constexpr std::size_t register_device_id = 0x008;
constexpr std::size_t register_device_features = 0x010;
constexpr std::size_t register_device_features_select = 0x014;
constexpr std::size_t register_driver_features = 0x020;
constexpr std::size_t register_driver_features_select = 0x024;
constexpr std::size_t register_guest_page_size = 0x028;
constexpr std::size_t register_queue_select = 0x030;
constexpr std::size_t register_queue_size_max = 0x034;
constexpr std::size_t register_queue_size = 0x038;
constexpr std::size_t register_queue_align = 0x03C;
constexpr std::size_t register_queue_pfn = 0x040;
constexpr std::size_t register_queue_notify = 0x050;
constexpr std::size_t register_status = 0x070;
constexpr std::size_t register_config = 0x100;

void barrier() { asm volatile("dsb sy" : : : "memory"); }

class MmioTransport final : public virtio::Transport {
public:
    constexpr MmioTransport() = default;
    void attach(std::uintptr_t base) { base_ = base; }

    std::uint32_t read(std::size_t offset) const {
        return *reinterpret_cast<volatile std::uint32_t*>(base_ + offset);
    }
    void write(std::size_t offset, std::uint32_t value) {
        *reinterpret_cast<volatile std::uint32_t*>(base_ + offset) = value;
    }

    std::uint32_t device_features() override {
        write(register_device_features_select, 0);
        return read(register_device_features);
    }
    void set_driver_features(std::uint32_t features) override {
        write(register_driver_features_select, 0);
        write(register_driver_features, features);
        // legacy MMIO 要求在設定佇列之前告訴裝置頁面大小。
        // Legacy MMIO wants the page size before any queue is set up.
        write(register_guest_page_size, static_cast<std::uint32_t>(virtio::queue_alignment));
    }
    std::uint8_t status() override { return static_cast<std::uint8_t>(read(register_status)); }
    void set_status(std::uint8_t status) override { write(register_status, status); }
    std::uint16_t queue_max(std::uint16_t queue) override {
        write(register_queue_select, queue);
        const auto maximum = read(register_queue_size_max);
        return maximum > 0xFFFF ? 0xFFFF : static_cast<std::uint16_t>(maximum);
    }
    bool queue_size_fixed() const override { return false; }
    bool set_queue(std::uint16_t queue, std::uint16_t size, std::uint64_t physical) override {
        const auto frame = physical / virtio::queue_alignment;
        if (frame > 0xFFFFFFFFu) return false;
        write(register_queue_select, queue);
        write(register_queue_size, size);
        write(register_queue_align, static_cast<std::uint32_t>(virtio::queue_alignment));
        barrier();
        write(register_queue_pfn, static_cast<std::uint32_t>(frame));
        return true;
    }
    // 佇列內容是一般記憶體，通知是裝置暫存器；dsb 確保裝置看到通知時，描述子
    // 與 ring 都已經寫進記憶體。
    //
    // The queue is ordinary memory and the notification a device register; the
    // dsb makes sure the descriptors and the ring have reached memory by the
    // time the device sees the notification.
    void notify(std::uint16_t queue) override {
        barrier();
        write(register_queue_notify, queue);
    }
    std::uint32_t config32(std::size_t offset) override { return read(register_config + offset); }

private:
    std::uintptr_t base_ = 0;
};

constinit MmioTransport transports[virtio::max_block_devices]{};
std::size_t transport_count = 0;

} // namespace

void virtio_mmio_probe(std::uintptr_t base, std::size_t slots, std::size_t stride) {
    for (std::size_t slot = slots; slot-- > 0;) {
        if (transport_count == virtio::max_block_devices) return;
        const auto address = base + slot * stride;
        auto& transport = transports[transport_count];
        transport.attach(address);
        if (transport.read(register_magic) != magic ||
            transport.read(register_device_id) != virtio::device_block)
            continue;
        if (transport.read(register_version) != 1) {
            console::write("[virtio] MMIO block device is not legacy (version 1); skipped\n");
            continue;
        }
        if (virtio::attach_block(transport)) ++transport_count;
    }
}

} // namespace shirley::platform::arm
