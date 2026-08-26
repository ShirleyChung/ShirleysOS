#include "shirley/format.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform/firmware/apple_boot_args.hpp"
#include "shirley/platform/firmware/e820.hpp"
#include "shirley/platform/firmware/fdt.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace shirley;
using namespace shirley::platform;

// 以大端序附加一個 32 位元欄位，用來組出測試用的裝置樹。
// Append a big-endian 32-bit field, used to build the test device tree.
void push_be32(std::vector<std::uint8_t>& blob, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        blob.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
}

void push_be64(std::vector<std::uint8_t>& blob, std::uint64_t value) {
    push_be32(blob, static_cast<std::uint32_t>(value >> 32));
    push_be32(blob, static_cast<std::uint32_t>(value));
}

// 字串在結構區塊中以 null 結尾並補齊到 4 位元組邊界。
// Strings in the structure block are null-terminated and padded to a 4-byte
// boundary.
void push_string(std::vector<std::uint8_t>& blob, const char* text) {
    for (const char* cursor = text; *cursor != '\0'; ++cursor)
        blob.push_back(static_cast<std::uint8_t>(*cursor));
    blob.push_back(0);
    while (blob.size() % 4 != 0) blob.push_back(0);
}

// 組出一個最小但結構完整的 DTB：根節點加上一個 /memory 節點。
// Build a minimal but structurally complete DTB: a root node plus one /memory
// node.
std::vector<std::uint8_t> build_device_tree(std::uint64_t base, std::uint64_t size,
                                            std::uint64_t reserved_base, std::uint64_t reserved_size) {
    // 字串區塊是一連串以 null 結尾的屬性名稱，屬性再以位移量引用它們。
    // The strings block is a run of null-terminated property names that
    // properties refer to by offset.
    const std::string strings = std::string("#address-cells\0#size-cells\0device_type\0reg\0", 43);
    const std::uint32_t offset_address_cells = 0;
    const std::uint32_t offset_size_cells = 15;
    const std::uint32_t offset_device_type = 27;
    const std::uint32_t offset_reg = 39;

    std::vector<std::uint8_t> structure;
    // FDT_BEGIN_NODE，根節點名稱為空字串
    // FDT_BEGIN_NODE; the root node's name is the empty string
    push_be32(structure, 1);
    push_string(structure, "");
    push_be32(structure, 3); // FDT_PROP #address-cells = 2
    push_be32(structure, 4);
    push_be32(structure, offset_address_cells);
    push_be32(structure, 2);
    push_be32(structure, 3); // FDT_PROP #size-cells = 2
    push_be32(structure, 4);
    push_be32(structure, offset_size_cells);
    push_be32(structure, 2);
    push_be32(structure, 1); // FDT_BEGIN_NODE memory@...
    push_string(structure, "memory@40000000");
    push_be32(structure, 3); // FDT_PROP device_type = "memory"
    push_be32(structure, 7);
    push_be32(structure, offset_device_type);
    push_string(structure, "memory");
    push_be32(structure, 3); // FDT_PROP reg = <base size>
    push_be32(structure, 16);
    push_be32(structure, offset_reg);
    push_be64(structure, base);
    push_be64(structure, size);
    push_be32(structure, 2); // FDT_END_NODE memory
    push_be32(structure, 2); // FDT_END_NODE root
    push_be32(structure, 9); // FDT_END

    std::vector<std::uint8_t> reservations;
    push_be64(reservations, reserved_base);
    push_be64(reservations, reserved_size);
    push_be64(reservations, 0);
    push_be64(reservations, 0);

    constexpr std::uint32_t header_size = 40;
    const auto reservation_offset = header_size;
    const auto struct_offset = reservation_offset + static_cast<std::uint32_t>(reservations.size());
    const auto strings_offset = struct_offset + static_cast<std::uint32_t>(structure.size());
    const auto total = strings_offset + static_cast<std::uint32_t>(strings.size());

    std::vector<std::uint8_t> blob;
    push_be32(blob, firmware::fdt_magic);
    push_be32(blob, total);
    push_be32(blob, struct_offset);
    push_be32(blob, strings_offset);
    push_be32(blob, reservation_offset);
    push_be32(blob, 17);
    push_be32(blob, 16);
    push_be32(blob, 0);
    push_be32(blob, static_cast<std::uint32_t>(strings.size()));
    push_be32(blob, static_cast<std::uint32_t>(structure.size()));
    blob.insert(blob.end(), reservations.begin(), reservations.end());
    blob.insert(blob.end(), structure.begin(), structure.end());
    blob.insert(blob.end(), strings.begin(), strings.end());
    assert(blob.size() == total);
    return blob;
}

void test_number_formatting() {
    char buffer[24];
    assert(format::to_decimal(buffer, sizeof(buffer), 0) == 1 && std::strcmp(buffer, "0") == 0);
    assert(format::to_decimal(buffer, sizeof(buffer), 4096) == 4 && std::strcmp(buffer, "4096") == 0);
    assert(format::to_decimal(buffer, sizeof(buffer), 18446744073709551615ull) == 20);
    assert(std::strcmp(buffer, "18446744073709551615") == 0);
    assert(format::to_hex(buffer, sizeof(buffer), 0xdeadbeef) == 8 &&
           std::strcmp(buffer, "deadbeef") == 0);
    assert(format::to_hex(buffer, sizeof(buffer), 0x10, 16) == 16);
    assert(std::strcmp(buffer, "0000000000000010") == 0);
    // 空間不足時輸出空字串而不是截斷的數字。
    // Too little space produces an empty string rather than a truncated number.
    char narrow[3];
    assert(format::to_decimal(narrow, sizeof(narrow), 12345) == 0 && narrow[0] == '\0');
}

void test_e820_conversion() {
    const firmware::E820Entry entries[] = {
        // 低位可用記憶體 / low usable memory
        {0x0, 0x9fc00, 1, 1},
        // EBDA，保留 / the EBDA, reserved
        {0x9fc00, 0x400, 2, 1},
        // 主要可用記憶體 / the main usable range
        {0x100000, 0x1ff00000, 1, 1},
        // ACPI 可回收 / ACPI reclaimable
        {0x20000000, 0x1000, 3, 1},
        // ACPI NVS，視為韌體 / ACPI NVS, treated as firmware
        {0x30000000, 0x1000, 4, 1},
        // 長度為零，應被略過 / zero length, must be skipped
        {0x40000000, 0x0, 1, 1},
        // ACPI 3.0 標記為無效，應被略過 / marked invalid by ACPI 3.0, skipped
        {0x50000000, 0x1000, 1, 0},
    };
    std::array<BootMemoryRegion, 16> regions{};
    const auto count = firmware::convert(entries, std::size(entries), regions.data(), regions.size());
    assert(count == 5);
    assert(regions[0].type == MemoryType::Usable && regions[0].length == 0x9fc00);
    assert(regions[1].type == MemoryType::Reserved);
    assert(regions[2].type == MemoryType::Usable && regions[2].physical_start == 0x100000);
    assert(regions[3].type == MemoryType::Reclaimable);
    assert(regions[4].type == MemoryType::Firmware);

    // 容量不足時只寫入放得下的部分，不會越界。
    // With too little capacity only what fits is written, never past the end.
    std::array<BootMemoryRegion, 2> tight{};
    assert(firmware::convert(entries, std::size(entries), tight.data(), tight.size()) == 2);
    assert(firmware::convert(nullptr, 4, regions.data(), regions.size()) == 0);
}

// 開機資訊轉成分頁分配器狀態後，保留區段必須真的被扣除。
// Once boot information reaches the page allocator, reserved ranges must
// really be subtracted.
void test_boot_info_drives_page_allocator() {
    const firmware::E820Entry entries[] = {
        {0x0, 0x100000, 1, 1},
        {0x100000, 0x400000, 1, 1},
    };
    std::array<BootMemoryRegion, 8> regions{};
    auto count = firmware::convert(entries, std::size(entries), regions.data(), regions.size());
    assert(count == 2);
    // 開機載入器與核心映像佔用 0 到 0x30000。
    // The boot loader and the kernel image occupy 0 through 0x30000.
    assert(firmware::append(regions.data(), count, regions.size(), 0, 0x30000, MemoryType::Reserved));

    BootInfo info{};
    info.memory_regions = regions.data();
    info.memory_region_count = count;
    memory::initialize(info);

    // 總共 0x500000 位元組，扣掉保留的 0x30000 之後剩下 1232 個頁面。
    // Of 0x500000 bytes, removing the reserved 0x30000 leaves 1232 pages.
    constexpr std::size_t expected_pages = (0x500000 - 0x30000) / memory::page_size;
    assert(memory::total_pages() == expected_pages);
    assert(memory::free_pages() == expected_pages);

    const auto first = memory::allocate_page();
    assert(first == 0x30000 && memory::free_pages() == expected_pages - 1);
    memory::free_page(first);
    assert(memory::free_pages() == expected_pages);
    // 保留範圍內的位址不屬於分配器，釋放請求必須被忽略。
    // An address inside a reserved range does not belong to the allocator, so
    // the free request must be ignored.
    memory::free_page(0x1000);
    assert(memory::free_pages() == expected_pages);
}

void test_device_tree_memory_map() {
    const auto blob = build_device_tree(0x40000000, 0x20000000, 0x40000000, 0x1000);
    assert(firmware::fdt_valid(blob.data()));
    assert(firmware::fdt_total_size(blob.data()) == blob.size());

    std::array<BootMemoryRegion, 8> regions{};
    const auto count = firmware::fdt_memory_map(blob.data(), regions.data(), regions.size());
    assert(count == 2);
    assert(regions[0].type == MemoryType::Usable);
    assert(regions[0].physical_start == 0x40000000 && regions[0].length == 0x20000000);
    assert(regions[1].type == MemoryType::Reserved);
    assert(regions[1].physical_start == 0x40000000 && regions[1].length == 0x1000);

    // 損壞或不是裝置樹的資料必須被拒絕，而不是讀到隨機記憶體。
    // Damaged data, or data that is not a device tree at all, must be rejected
    // rather than read as if it were one.
    auto damaged = blob;
    damaged[0] = 0;
    assert(!firmware::fdt_valid(damaged.data()));
    assert(firmware::fdt_memory_map(damaged.data(), regions.data(), regions.size()) == 0);
    assert(!firmware::fdt_valid(nullptr));

    // 宣告的長度超過實際內容時同樣視為不合法。
    // A declared size larger than the actual content is invalid too.
    auto truncated = blob;
    truncated[8] = 0xff;
    assert(!firmware::fdt_valid(truncated.data()));
}

void test_apple_boot_args() {
    firmware::AppleBootArgs arguments{};
    arguments.revision = 2;
    arguments.version = 2;
    arguments.physical_base = 0x800000000ull;
    arguments.memory_size = 0x200000000ull;
    arguments.top_of_kernel_data = 0x800400000ull;
    arguments.video.base_address = 0x900000000ull;
    arguments.video.width = 2560;
    arguments.video.height = 1600;
    arguments.video.stride = 2560 * 4;
    arguments.video.depth = 30;
    arguments.device_tree = 0x8003f0000ull;
    arguments.device_tree_size = 0x10000;

    assert(firmware::apple_boot_args_valid(&arguments));
    std::array<BootMemoryRegion, 8> regions{};
    const auto count = firmware::apple_boot_args_memory_map(&arguments, regions.data(), regions.size());
    assert(count == 4);
    assert(regions[0].type == MemoryType::Usable && regions[0].length == 0x200000000ull);
    assert(regions[1].type == MemoryType::Reserved && regions[1].length == 0x400000);
    assert(regions[2].type == MemoryType::Reserved && regions[2].length == 2560 * 4 * 1600);
    assert(regions[3].type == MemoryType::Firmware);

    FramebufferInfo framebuffer{};
    assert(firmware::apple_boot_args_framebuffer(&arguments, framebuffer));
    assert(framebuffer.width == 2560 && framebuffer.height == 1600);
    assert(framebuffer.pitch == 2560 * 4 && framebuffer.format == 30);

    // 太舊的結構版本沒有這些欄位，必須拒絕而不是讀出垃圾。
    // An older structure revision does not have these fields, so it must be
    // rejected rather than read as garbage.
    auto old = arguments;
    old.revision = 1;
    assert(!firmware::apple_boot_args_valid(&old));
    assert(firmware::apple_boot_args_memory_map(&old, regions.data(), regions.size()) == 0);
    assert(!firmware::apple_boot_args_valid(nullptr));

    // top_of_kernel_data 落在 RAM 之外代表結構不可信。
    // A top_of_kernel_data outside RAM means the structure cannot be trusted.
    auto inconsistent = arguments;
    inconsistent.top_of_kernel_data = 0x100;
    assert(!firmware::apple_boot_args_valid(&inconsistent));
}

} // namespace

int main() {
    test_number_formatting();
    test_e820_conversion();
    test_boot_info_drives_page_allocator();
    test_device_tree_memory_map();
    test_apple_boot_args();
    return 0;
}
