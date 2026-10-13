#include "shirley/platform/pc/pci.hpp"

#include "shirley/arch/x86_64/port_io.hpp"

namespace shirley::platform::pc {
namespace {

using arch::x86_64::inl;
using arch::x86_64::outl;

constexpr std::uint16_t config_address = 0xCF8;
constexpr std::uint16_t config_data = 0xCFC;
constexpr std::uint16_t no_device = 0xFFFF;

std::uint32_t encode(PciAddress address, std::uint8_t offset) {
    return 0x80000000u | (static_cast<std::uint32_t>(address.bus) << 16) |
           (static_cast<std::uint32_t>(address.device & 0x1F) << 11) |
           (static_cast<std::uint32_t>(address.function & 0x07) << 8) | (offset & 0xFC);
}

} // namespace

std::uint32_t pci_read32(PciAddress address, std::uint8_t offset) {
    outl(config_address, encode(address, offset));
    return inl(config_data);
}

void pci_write32(PciAddress address, std::uint8_t offset, std::uint32_t value) {
    outl(config_address, encode(address, offset));
    outl(config_data, value);
}

std::uint16_t pci_read16(PciAddress address, std::uint8_t offset) {
    return static_cast<std::uint16_t>(pci_read32(address, offset) >> ((offset & 2) * 8));
}

// 設定空間只能以 32 位元存取，所以寫 16 位元要先讀出整個字再改一半。
// Configuration space is accessed 32 bits at a time, so a 16-bit write reads
// the whole word and changes half of it.
void pci_write16(PciAddress address, std::uint8_t offset, std::uint16_t value) {
    const auto shift = (offset & 2) * 8;
    auto word = pci_read32(address, offset);
    word = (word & ~(0xFFFFu << shift)) | (static_cast<std::uint32_t>(value) << shift);
    pci_write32(address, offset, word);
}

void pci_scan(PciVisitor visit) {
    for (std::uint8_t device = 0; device < 32; ++device) {
        PciAddress address{0, device, 0};
        const auto id = pci_read32(address, 0);
        if ((id & 0xFFFF) == no_device) continue;
        // 標頭類型的最高位元表示這是多功能裝置，才需要看功能 1 到 7。
        // The header type's top bit marks a multi-function device, the only
        // kind whose functions 1 to 7 are worth looking at.
        const bool multi = ((pci_read32(address, 0x0C) >> 16) & 0x80) != 0;
        for (std::uint8_t function = 0; function < (multi ? 8 : 1); ++function) {
            address.function = function;
            const auto value = function == 0 ? id : pci_read32(address, 0);
            if ((value & 0xFFFF) == no_device) continue;
            visit(address, static_cast<std::uint16_t>(value), static_cast<std::uint16_t>(value >> 16));
        }
    }
}

} // namespace shirley::platform::pc
