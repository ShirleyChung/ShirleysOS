#include "shirley/checksum.hpp"

namespace shirley::checksum {
namespace {

// 逐位元組查表。表在編譯期算好，放在 .rodata，不需要初始化步驟。
// A byte-at-a-time table, computed at compile time into .rodata so no
// initialization step is needed.
struct Table {
    std::uint32_t entries[256];
};

constexpr Table make_table() {
    Table table{};
    for (std::uint32_t index = 0; index < 256; ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) value = (value & 1) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
        table.entries[index] = value;
    }
    return table;
}

constexpr Table table = make_table();

} // namespace

std::uint32_t crc32(const void* data, std::size_t length, std::uint32_t previous) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t value = ~previous;
    for (std::size_t i = 0; i < length; ++i)
        value = table.entries[(value ^ bytes[i]) & 0xFF] ^ (value >> 8);
    return ~value;
}

} // namespace shirley::checksum
