#include "shirley/boot/elf64.hpp"
#include "shirley/boot_protocol.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform/firmware/uefi.hpp"

#include <array>
#include <cassert>
#include <cstring>
#include <vector>

namespace {

using namespace shirley;
using namespace shirley::boot;
using namespace shirley::platform;

// 組出一份最小的 ELF64 執行檔，欄位配置與真實 ELF 相同。
// Build a minimal ELF64 executable whose field layout matches a real one.
struct BuiltElf {
    std::vector<std::uint8_t> bytes;
    std::uint64_t entry = 0;
};

constexpr std::size_t elf_header_size = 64;
constexpr std::size_t program_header_size = 56;

void write64(std::vector<std::uint8_t>& blob, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) blob[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void write32(std::vector<std::uint8_t>& blob, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) blob[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
void write16(std::vector<std::uint8_t>& blob, std::size_t offset, std::uint16_t value) {
    for (unsigned i = 0; i < 2; ++i) blob[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

// program_header_stride 可以大於實際結構，用來模擬宣告了較大 entry size 的 ELF。
// program_header_stride may exceed the real structure, to model an ELF that
// declares a larger entry size.
BuiltElf build_elf(std::uint16_t machine, std::uint64_t load_address, std::uint64_t file_size,
                   std::uint64_t memory_size, std::size_t program_header_stride = program_header_size,
                   std::uint16_t type = 2) {
    BuiltElf built;
    const std::size_t table_offset = elf_header_size;
    const std::size_t payload_offset = table_offset + program_header_stride;
    built.bytes.assign(payload_offset + static_cast<std::size_t>(file_size), 0);
    built.entry = load_address + 0x10;

    auto& blob = built.bytes;
    blob[0] = 0x7f; blob[1] = 'E'; blob[2] = 'L'; blob[3] = 'F';
    // 64 位元、小端序、目前的 ELF 版本。
    // 64-bit, little-endian, current ELF version.
    blob[4] = 2; blob[5] = 1; blob[6] = 1;
    write16(blob, 16, type);
    write16(blob, 18, machine);
    write32(blob, 20, 1);
    write64(blob, 24, built.entry);
    write64(blob, 32, table_offset);
    write16(blob, 52, elf_header_size);
    write16(blob, 54, static_cast<std::uint16_t>(program_header_stride));
    write16(blob, 56, 1);

    // 唯一一個 PT_LOAD，權限為可讀可執行。
    // The single PT_LOAD, readable and executable.
    write32(blob, table_offset + 0, 1);
    write32(blob, table_offset + 4, 0x4 | 0x1);
    write64(blob, table_offset + 8, payload_offset);
    write64(blob, table_offset + 16, load_address);
    write64(blob, table_offset + 24, load_address);
    write64(blob, table_offset + 32, file_size);
    write64(blob, table_offset + 40, memory_size);
    write64(blob, table_offset + 48, 4096);
    return built;
}

void test_elf64_reader() {
    const auto image = build_elf(elf_machine_x86_64, 0x200000, 0x1000, 0x3000);
    assert(elf64_valid(image.bytes.data(), image.bytes.size(), elf_machine_x86_64));
    assert(elf64_entry(image.bytes.data(), image.bytes.size(), elf_machine_x86_64) == image.entry);

    Elf64Segment segments[4]{};
    const auto count =
        elf64_segments(image.bytes.data(), image.bytes.size(), elf_machine_x86_64, segments, 4);
    assert(count == 1);
    assert(segments[0].physical_address == 0x200000);
    assert(segments[0].file_size == 0x1000 && segments[0].memory_size == 0x3000);
    assert(segments[0].executable && segments[0].readable && !segments[0].writable);

    std::uint64_t lowest = 0;
    std::uint64_t end = 0;
    assert(elf64_physical_extent(segments, count, lowest, end));
    assert(lowest == 0x200000 && end == 0x203000);

    // 架構不符的 ELF 必須被拒絕，否則會把 ARM64 核心載進 x86 機器。
    // An ELF for another architecture must be rejected, or an ARM64 kernel
    // could be loaded onto an x86 machine.
    assert(!elf64_valid(image.bytes.data(), image.bytes.size(), elf_machine_aarch64));

    // 宣告的 entry size 較大時仍要能正確走訪 program header。
    // A larger declared entry size must still be walked correctly.
    const auto padded = build_elf(elf_machine_aarch64, 0x41000000, 0x800, 0x800, 72);
    Elf64Segment wide[4]{};
    assert(elf64_segments(padded.bytes.data(), padded.bytes.size(), elf_machine_aarch64, wide, 4) == 1);
    assert(wide[0].physical_address == 0x41000000);

    // 截斷的檔案、非執行檔、以及超出檔案範圍的節區都必須被拒絕。
    // A truncated file, a non-executable, and a segment reaching past the end
    // of the file must all be rejected.
    assert(!elf64_valid(image.bytes.data(), 8, elf_machine_x86_64));
    assert(!elf64_valid(nullptr, 128, elf_machine_x86_64));
    const auto shared_object = build_elf(elf_machine_x86_64, 0x200000, 0x1000, 0x3000,
                                         program_header_size, 3);
    assert(!elf64_valid(shared_object.bytes.data(), shared_object.bytes.size(), elf_machine_x86_64));
    auto overflowing = image;
    // 把 p_filesz 改成遠大於檔案本身的值。
    // Grow p_filesz far beyond the file itself.
    write64(overflowing.bytes, elf_header_size + 32, 0xffffffff);
    assert(!elf64_valid(overflowing.bytes.data(), overflowing.bytes.size(), elf_machine_x86_64));
    // memory_size 小於 file_size 沒有意義。
    // A memory_size below file_size is meaningless.
    auto shrinking = image;
    write64(shrinking.bytes, elf_header_size + 40, 0x10);
    assert(!elf64_valid(shrinking.bytes.data(), shrinking.bytes.size(), elf_machine_x86_64));
}

void test_uefi_memory_map() {
    using firmware::EfiMemoryDescriptor;
    using firmware::efi_page_size;

    // 韌體常常使用比結構本身更大的描述元，因此步幅刻意設得比 sizeof 大。
    // Firmware often uses a descriptor larger than the structure itself, so
    // the stride here is deliberately bigger than sizeof.
    constexpr std::uint64_t stride = sizeof(EfiMemoryDescriptor) + 8;
    constexpr unsigned entries = 6;
    std::vector<std::uint8_t> map(stride * entries, 0xcd);

    const EfiMemoryDescriptor source[entries] = {
        {firmware::efi_conventional_memory, 0, 0x100000, 0, 0x100, 0},
        {firmware::efi_loader_data, 0, 0x200000, 0, 0x10, 0},
        {firmware::efi_boot_services_data, 0, 0x300000, 0, 0x20, 0},
        {firmware::efi_acpi_reclaim_memory, 0, 0x400000, 0, 0x2, 0},
        {firmware::efi_memory_mapped_io, 0, 0xfec00000, 0, 0x1, 0},
        // 長度為零的描述元應該被略過。
        // A descriptor with no pages must be skipped.
        {firmware::efi_conventional_memory, 0, 0x500000, 0, 0, 0},
    };
    for (unsigned i = 0; i < entries; ++i)
        std::memcpy(map.data() + i * stride, &source[i], sizeof(EfiMemoryDescriptor));

    std::array<BootMemoryRegion, 16> regions{};
    const auto count =
        firmware::uefi_memory_map(map.data(), map.size(), stride, regions.data(), regions.size());
    assert(count == 5);
    assert(regions[0].type == MemoryType::Usable);
    assert(regions[0].physical_start == 0x100000 && regions[0].length == 0x100 * efi_page_size);
    // 載入器自己的資料仍在使用中，不能當成可用記憶體。
    // The loader's own data is still live and must not be usable.
    assert(regions[1].type == MemoryType::Reserved);
    // boot services 的記憶體在 ExitBootServices 之後就可以回收。
    // Boot services memory becomes reusable once ExitBootServices has run.
    assert(regions[2].type == MemoryType::Usable);
    assert(regions[3].type == MemoryType::Reclaimable);
    assert(regions[4].type == MemoryType::Mmio);

    // 用 sizeof 而不是韌體回報的步幅走訪就會讀錯，因此步幅太小必須直接拒絕。
    // Walking with sizeof instead of the firmware-reported stride misreads the
    // map, so a stride that is too small is rejected outright.
    assert(firmware::uefi_memory_map(map.data(), map.size(), 8, regions.data(), regions.size()) == 0);
    assert(firmware::uefi_memory_map(nullptr, 64, stride, regions.data(), regions.size()) == 0);

    // 不完整的最後一筆描述元不會被讀取。
    // A trailing partial descriptor is not read.
    assert(firmware::uefi_memory_map(map.data(), stride * 2 + 4, stride, regions.data(),
                                     regions.size()) == 2);
}

// UEFI 記憶體地圖必須能直接驅動分頁分配器。
// A UEFI memory map has to drive the page allocator directly.
void test_uefi_map_drives_page_allocator() {
    using firmware::EfiMemoryDescriptor;
    constexpr std::uint64_t stride = sizeof(EfiMemoryDescriptor);
    const EfiMemoryDescriptor source[] = {
        {firmware::efi_conventional_memory, 0, 0x100000, 0, 0x100, 0},
        {firmware::efi_loader_data, 0, 0x180000, 0, 0x10, 0},
    };
    std::array<BootMemoryRegion, 8> regions{};
    const auto count = firmware::uefi_memory_map(source, sizeof(source), stride, regions.data(),
                                                 regions.size());
    assert(count == 2);

    BootInfo info{};
    info.memory_regions = regions.data();
    info.memory_region_count = count;
    memory::initialize(info);

    // 可用區段有 0x100 頁，其中被載入器佔用的 0x10 頁必須扣掉。
    // The usable range is 0x100 pages, and the 0x10 the loader occupies inside
    // it must be subtracted.
    assert(memory::total_pages() == 0x100 - 0x10);
    // 被保留的位址絕對不能被配置出去。
    // A reserved address must never be handed out.
    for (std::size_t i = 0; i < memory::total_pages(); ++i) {
        const auto page = memory::allocate_page();
        assert(page != 0);
        assert(page < 0x180000 || page >= 0x190000);
    }
    assert(memory::allocate_page() == 0);
}

void test_boot_handoff() {
    BootHandoff handoff{};
    assert(handoff.magic == BootHandoff::magic_value);
    assert(handoff.size == sizeof(BootHandoff));
    assert(boot_handoff_valid(&handoff));

    // magic 存放的正是 "SHIRLEY0" 的位元組序列。
    // The magic really is the byte sequence of "SHIRLEY0".
    char text[9] = {};
    std::memcpy(text, &handoff.magic, 8);
    assert(std::strcmp(text, "SHIRLEY0") == 0);

    // 韌體殘留的指標不會通過驗證，核心因此不會誤用它。
    // A stale firmware pointer does not validate, so the kernel cannot misuse
    // it.
    assert(!boot_handoff_valid(nullptr));
    auto wrong_magic = handoff;
    wrong_magic.magic ^= 1;
    assert(!boot_handoff_valid(&wrong_magic));
    auto wrong_version = handoff;
    wrong_version.version = BootHandoff::current_version + 1;
    assert(!boot_handoff_valid(&wrong_version));
    auto truncated = handoff;
    truncated.size = sizeof(BootHandoff) - 1;
    assert(!boot_handoff_valid(&truncated));
    auto stale_info = handoff;
    stale_info.info.version = BootInfo::current_version + 1;
    assert(!boot_handoff_valid(&stale_info));
}

} // namespace

int main() {
    test_elf64_reader();
    test_uefi_memory_map();
    test_uefi_map_drives_page_allocator();
    test_boot_handoff();
    return 0;
}
