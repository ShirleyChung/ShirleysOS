#include "shirley/arch/x86_64/port_io.hpp"
#include "shirley/console.hpp"
#include "shirley/platform/pc/pci.hpp"
#include "shirley/virtio.hpp"

// legacy virtio over PCI：暫存器在 BAR0 指向的一段 I/O port 裡。現代（1.0）的
// virtio-pci 把暫存器放在記憶體 BAR，通常落在 4 GiB 附近，超出開機時 1 GiB 的
// identity map；legacy 介面走 port I/O，完全不需要映射。QEMU 的 virtio-blk-pci
// 預設是 transitional，兩種介面都有。
//
// Legacy virtio over PCI: the registers sit in a range of I/O ports that BAR0
// points at. Modern (1.0) virtio-pci puts them in a memory BAR, usually near
// 4 GiB and outside the 1 GiB identity map set up at boot; the legacy interface
// goes through port I/O and needs no mapping at all. QEMU's virtio-blk-pci is
// transitional by default and offers both.
namespace shirley::platform::pc {
namespace {

using arch::x86_64::inb;
using arch::x86_64::inl;
using arch::x86_64::inw;
using arch::x86_64::outb;
using arch::x86_64::outl;
using arch::x86_64::outw;

constexpr std::uint16_t virtio_vendor = 0x1AF4;
// transitional virtio-blk 的 PCI 裝置編號；只有現代介面的裝置是 0x1042。
// The PCI device ID of a transitional virtio-blk; a modern-only one is 0x1042.
constexpr std::uint16_t virtio_block_legacy = 0x1001;

// legacy 暫存器在 I/O 區段裡的位置。沒有啟用 MSI-X 時，裝置設定空間從 0x14
// 開始。
// The legacy registers' offsets in the I/O range. Without MSI-X enabled the
// device configuration space begins at 0x14.
constexpr std::uint16_t register_device_features = 0x00;
constexpr std::uint16_t register_driver_features = 0x04;
constexpr std::uint16_t register_queue_pfn = 0x08;
constexpr std::uint16_t register_queue_size = 0x0C;
constexpr std::uint16_t register_queue_select = 0x0E;
constexpr std::uint16_t register_queue_notify = 0x10;
constexpr std::uint16_t register_status = 0x12;
constexpr std::uint16_t register_config = 0x14;

constexpr std::uint16_t command_io_space = 1u << 0;
constexpr std::uint16_t command_bus_master = 1u << 2;

class LegacyPciTransport final : public virtio::Transport {
public:
    constexpr LegacyPciTransport() = default;
    void attach(std::uint16_t base) { base_ = base; }

    std::uint32_t device_features() override { return inl(base_ + register_device_features); }
    void set_driver_features(std::uint32_t features) override {
        outl(base_ + register_driver_features, features);
    }
    std::uint8_t status() override { return inb(base_ + register_status); }
    void set_status(std::uint8_t status) override { outb(base_ + register_status, status); }
    std::uint16_t queue_max(std::uint16_t queue) override {
        outw(base_ + register_queue_select, queue);
        return inw(base_ + register_queue_size);
    }
    bool queue_size_fixed() const override { return true; }
    bool set_queue(std::uint16_t queue, std::uint16_t, std::uint64_t physical) override {
        // legacy 介面以頁框號碼描述佇列位置，只有 32 位元。
        // The legacy interface names the queue by page frame number, in 32 bits.
        const auto frame = physical / virtio::queue_alignment;
        if (frame > 0xFFFFFFFFu) return false;
        outw(base_ + register_queue_select, queue);
        outl(base_ + register_queue_pfn, static_cast<std::uint32_t>(frame));
        return true;
    }
    // port I/O 在 x86 上本身就是序列化的，寫入通知之前不需要額外的屏障。
    // Port I/O is serializing on x86, so the notification needs no extra
    // barrier before it.
    void notify(std::uint16_t queue) override { outw(base_ + register_queue_notify, queue); }
    std::uint32_t config32(std::size_t offset) override {
        return inl(static_cast<std::uint16_t>(base_ + register_config + offset));
    }

private:
    std::uint16_t base_ = 0;
};

constinit LegacyPciTransport transports[virtio::max_block_devices]{};
std::size_t transport_count = 0;

void visit(PciAddress address, std::uint16_t vendor, std::uint16_t device) {
    if (vendor != virtio_vendor || device != virtio_block_legacy) return;
    if (transport_count == virtio::max_block_devices) return;
    const auto bar = pci_read32(address, 0x10);
    // BAR0 必須是 I/O 空間；不是的話這不是一個能用 legacy 介面的裝置。
    // BAR0 has to be I/O space; otherwise the device cannot be driven through
    // the legacy interface.
    if ((bar & 1) == 0 || (bar & 0xFFFC) == 0) {
        console::write("[virtio] PCI block device without a legacy I/O BAR; skipped\n");
        return;
    }
    pci_write16(address, 0x04,
                static_cast<std::uint16_t>(pci_read16(address, 0x04) | command_io_space |
                                           command_bus_master));
    auto& transport = transports[transport_count];
    transport.attach(static_cast<std::uint16_t>(bar & 0xFFFC));
    if (virtio::attach_block(transport)) ++transport_count;
}

} // namespace

void virtio_pci_probe() { pci_scan(visit); }

} // namespace shirley::platform::pc
