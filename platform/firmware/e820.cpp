#include "shirley/platform/firmware/e820.hpp"

namespace shirley::platform::firmware {
namespace {

// ACPI 3.0 在屬性欄位的位元 0 標示此項目是否應該被採用。
// Bit 0 of the ACPI 3.0 attribute field says whether the entry should be used.
constexpr std::uint32_t attribute_valid = 1u << 0;

} // namespace

MemoryType classify(std::uint32_t e820_type) {
    switch (e820_type) {
        case 1: return MemoryType::Usable;
        case 3: return MemoryType::Reclaimable;
        case 4: return MemoryType::Firmware;
        // 型別 2、5 與所有未知型別一律視為不可使用。
        // Types 2 and 5, and every unknown type, are treated as unusable.
        default: return MemoryType::Reserved;
    }
}

std::uint64_t convert(const E820Entry* entries, std::uint64_t entry_count,
                      BootMemoryRegion* regions, std::uint64_t capacity) {
    if (entries == nullptr || regions == nullptr) return 0;
    std::uint64_t count = 0;
    for (std::uint64_t i = 0; i < entry_count && count < capacity; ++i) {
        const auto& entry = entries[i];
        if (entry.length == 0) continue;
        if ((entry.attributes & attribute_valid) == 0) continue;
        regions[count++] = {entry.base, entry.length, classify(entry.type)};
    }
    return count;
}

bool append(BootMemoryRegion* regions, std::uint64_t& count, std::uint64_t capacity,
            std::uint64_t start, std::uint64_t length, MemoryType type) {
    if (regions == nullptr || length == 0 || count >= capacity) return false;
    regions[count++] = {start, length, type};
    return true;
}

} // namespace shirley::platform::firmware
