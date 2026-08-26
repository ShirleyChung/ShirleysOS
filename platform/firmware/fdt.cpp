#include "shirley/platform/firmware/fdt.hpp"

namespace shirley::platform::firmware {
namespace {

// 結構區塊的權杖。
// The tokens that make up the structure block.
constexpr std::uint32_t token_begin_node = 1;
constexpr std::uint32_t token_end_node = 2;
constexpr std::uint32_t token_property = 3;
constexpr std::uint32_t token_nop = 4;
constexpr std::uint32_t token_end = 9;

// DTB 內所有整數都是大端序。
// Every integer inside a DTB is big-endian.
struct Header {
    std::uint32_t magic;
    std::uint32_t total_size;
    std::uint32_t struct_offset;
    std::uint32_t strings_offset;
    std::uint32_t reservation_offset;
    std::uint32_t version;
    std::uint32_t last_compatible_version;
    std::uint32_t boot_cpu;
    std::uint32_t strings_size;
    std::uint32_t struct_size;
};

std::uint32_t read_be32(const std::uint8_t* source) {
    return (static_cast<std::uint32_t>(source[0]) << 24) | (static_cast<std::uint32_t>(source[1]) << 16) |
           (static_cast<std::uint32_t>(source[2]) << 8) | static_cast<std::uint32_t>(source[3]);
}

std::uint64_t read_be64(const std::uint8_t* source) {
    return (static_cast<std::uint64_t>(read_be32(source)) << 32) | read_be32(source + 4);
}

Header read_header(const std::uint8_t* blob) {
    return {read_be32(blob), read_be32(blob + 4), read_be32(blob + 8), read_be32(blob + 12),
            read_be32(blob + 16), read_be32(blob + 20), read_be32(blob + 24), read_be32(blob + 28),
            read_be32(blob + 32), read_be32(blob + 36)};
}

constexpr std::uint32_t align4(std::uint32_t value) { return (value + 3) & ~3u; }

// 只比較到結尾或 '@' 之前，因為節點名稱常帶有單位位址後綴。
// Compare only up to the end or an '@', because node names usually carry a
// unit address suffix.
bool node_name_is(const char* name, const char* expected, std::uint32_t available) {
    std::uint32_t i = 0;
    for (; expected[i] != '\0'; ++i) {
        if (i >= available || name[i] != expected[i]) return false;
    }
    return i >= available || name[i] == '\0' || name[i] == '@';
}

bool string_equals(const char* left, const char* right, std::uint32_t available) {
    std::uint32_t i = 0;
    for (; right[i] != '\0'; ++i) {
        if (i >= available || left[i] != right[i]) return false;
    }
    return i < available && left[i] == '\0';
}

// 依 #address-cells / #size-cells 讀出一個位址或長度欄位。
// Read one address or length field, sized by #address-cells / #size-cells.
std::uint64_t read_cells(const std::uint8_t* data, std::uint32_t cells) {
    if (cells == 1) return read_be32(data);
    if (cells == 2) return read_be64(data);
    return 0;
}

// DTB 來自韌體，每個宣告的位移與長度都必須落在 blob 之內。
// The DTB comes from firmware, so every declared offset and length must land
// inside the blob.
bool header_is_sane(const Header& header) {
    if (header.magic != fdt_magic || header.total_size < sizeof(Header)) return false;
    if (header.struct_offset > header.total_size || header.strings_offset > header.total_size) return false;
    if (header.struct_size > header.total_size - header.struct_offset) return false;
    if (header.strings_size > header.total_size - header.strings_offset) return false;
    return true;
}

bool append(BootMemoryRegion* regions, std::uint64_t& count, std::uint64_t capacity,
            std::uint64_t start, std::uint64_t length, MemoryType type) {
    if (length == 0 || count >= capacity) return false;
    regions[count++] = {start, length, type};
    return true;
}

// 記憶體保留區塊是一連串 (位址, 長度) 配對，以兩個 0 結束。
// The reservation block is a list of (address, length) pairs terminated by a
// pair of zeros.
void read_reservations(const std::uint8_t* blob, const Header& header, BootMemoryRegion* regions,
                       std::uint64_t& count, std::uint64_t capacity) {
    auto offset = header.reservation_offset;
    while (offset + 16 <= header.total_size && count < capacity) {
        const auto address = read_be64(blob + offset);
        const auto length = read_be64(blob + offset + 8);
        offset += 16;
        if (address == 0 && length == 0) return;
        append(regions, count, capacity, address, length, MemoryType::Reserved);
    }
}

} // namespace

bool fdt_valid(const void* blob) {
    if (blob == nullptr) return false;
    return header_is_sane(read_header(static_cast<const std::uint8_t*>(blob)));
}

std::uint64_t fdt_total_size(const void* blob) {
    if (!fdt_valid(blob)) return 0;
    return read_header(static_cast<const std::uint8_t*>(blob)).total_size;
}

std::uint64_t fdt_memory_map(const void* blob, BootMemoryRegion* regions, std::uint64_t capacity) {
    if (blob == nullptr || regions == nullptr || capacity == 0) return 0;
    const auto* bytes = static_cast<const std::uint8_t*>(blob);
    const auto header = read_header(bytes);
    if (!header_is_sane(header)) return 0;

    const auto* structure = bytes + header.struct_offset;
    const auto* strings = reinterpret_cast<const char*>(bytes + header.strings_offset);
    std::uint64_t count = 0;

    // 根節點沒有指定時，位址與長度都預設為兩個 32 位元欄位。
    // When the root node says nothing, addresses and lengths both default to
    // two 32-bit cells.
    std::uint32_t address_cells = 2;
    std::uint32_t size_cells = 2;
    unsigned depth = 0;
    unsigned memory_depth = 0;
    bool in_memory_node = false;

    std::uint32_t offset = 0;
    while (offset + 4 <= header.struct_size) {
        const auto token = read_be32(structure + offset);
        offset += 4;
        if (token == token_end) break;
        if (token == token_nop) continue;

        if (token == token_begin_node) {
            const auto* name = reinterpret_cast<const char*>(structure + offset);
            std::uint32_t length = 0;
            while (offset + length < header.struct_size && name[length] != '\0') ++length;
            if (offset + length >= header.struct_size) break;
            ++depth;
            // 根節點在深度 1，因此 /memory 節點位於深度 2。
            // The root node sits at depth 1, so /memory is at depth 2.
            if (!in_memory_node && depth == 2 && node_name_is(name, "memory", length)) {
                in_memory_node = true;
                memory_depth = depth;
            }
            offset += align4(length + 1);
            continue;
        }

        if (token == token_end_node) {
            if (in_memory_node && depth == memory_depth) in_memory_node = false;
            if (depth == 0) break;
            --depth;
            continue;
        }

        // 出現未知權杖代表結構已經無法信任，停止解析。
        // An unknown token means the structure can no longer be trusted, so
        // stop parsing.
        if (token != token_property) break;
        if (offset + 8 > header.struct_size) break;
        const auto length = read_be32(structure + offset);
        const auto name_offset = read_be32(structure + offset + 4);
        offset += 8;
        if (length > header.struct_size - offset || name_offset >= header.strings_size) break;
        const auto* data = structure + offset;
        const auto* property = strings + name_offset;
        const auto property_space = header.strings_size - name_offset;

        if (depth == 1 && length == 4) {
            if (string_equals(property, "#address-cells", property_space)) address_cells = read_be32(data);
            else if (string_equals(property, "#size-cells", property_space)) size_cells = read_be32(data);
        } else if (in_memory_node && string_equals(property, "reg", property_space)) {
            const auto stride = (address_cells + size_cells) * 4;
            if (stride != 0 && address_cells <= 2 && size_cells <= 2) {
                for (std::uint32_t position = 0; position + stride <= length; position += stride) {
                    const auto base = read_cells(data + position, address_cells);
                    const auto size = read_cells(data + position + address_cells * 4, size_cells);
                    if (!append(regions, count, capacity, base, size, MemoryType::Usable)) break;
                }
            }
        }
        offset += align4(length);
    }

    read_reservations(bytes, header, regions, count, capacity);
    return count;
}

} // namespace shirley::platform::firmware
