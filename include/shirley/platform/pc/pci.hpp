#pragma once

#include <cstdint>

// PC 的 PCI 設定空間，透過傳統的 0xCF8／0xCFC 設定機制存取。
//
// 只做到找出 virtio 裝置需要的程度：讀寫設定空間，以及走過匯流排 0 上的每個
// 裝置與功能。QEMU 的 pc 與 q35 機器預設把裝置都放在匯流排 0；橋接器後面的
// 匯流排、ECAM 與 MSI 要等實體 PC（M7）。
//
// The PC's PCI configuration space, reached through the legacy 0xCF8/0xCFC
// configuration mechanism.
//
// It goes only as far as finding virtio devices needs: reading and writing
// configuration space, and walking every device and function on bus 0. QEMU's
// pc and q35 machines put their devices on bus 0 by default; buses behind
// bridges, ECAM and MSI wait for physical PCs (M7).
namespace shirley::platform::pc {

struct PciAddress {
    std::uint8_t bus = 0;
    std::uint8_t device = 0;
    std::uint8_t function = 0;
};

std::uint32_t pci_read32(PciAddress address, std::uint8_t offset);
void pci_write32(PciAddress address, std::uint8_t offset, std::uint32_t value);
std::uint16_t pci_read16(PciAddress address, std::uint8_t offset);
void pci_write16(PciAddress address, std::uint8_t offset, std::uint16_t value);

// 對匯流排 0 上每個存在的功能呼叫一次 visit。
// Call visit once for every function present on bus 0.
using PciVisitor = void (*)(PciAddress address, std::uint16_t vendor, std::uint16_t device);
void pci_scan(PciVisitor visit);

// 找到所有 legacy virtio-blk 裝置並交給 virtio 驅動程式。需要堆積。
// Find every legacy virtio-blk device and hand it to the virtio driver. Needs
// the heap.
void virtio_pci_probe();

} // namespace shirley::platform::pc
