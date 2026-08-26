#include "shirley/boot_info.hpp"
#include "shirley/platform/firmware/e820.hpp"

// 由連結腳本提供，標示核心映像（含 .bss）在實體記憶體中的結尾。
// Provided by the linker script: where the kernel image, including .bss, ends
// in physical memory.
extern "C" const char __kernel_end[];

namespace shirley::platform {
namespace {

// 必須與 arch/x86_64/boot.S 中開機磁區寫入的位址一致。
// Must match the address the boot sector in arch/x86_64/boot.S writes to.
constexpr std::uintptr_t firmware_memory_map = 0x8000;
// E820 項目數上限 64，再加上核心自行追加的保留區段。
// The boot sector collects at most 64 E820 entries, plus the reserved regions
// the kernel appends itself.
constexpr std::uint64_t max_regions = 72;

BootMemoryRegion regions[max_regions];
BootInfo boot_info;

} // namespace
} // namespace shirley::platform

// 由 arch/x86_64/entry.S 呼叫，把 BIOS 的記憶體地圖轉成通用 BootInfo。
// Called from arch/x86_64/entry.S to turn the BIOS memory map into a neutral
// BootInfo.
extern "C" const shirley::BootInfo* shirley_platform_boot_info(const void* firmware_table) {
    using namespace shirley;
    using namespace shirley::platform;

    const auto* table = static_cast<const firmware::E820Table*>(
        firmware_table != nullptr ? firmware_table
                                  : reinterpret_cast<const void*>(firmware_memory_map));
    auto count = firmware::convert(table->entries, table->entry_count, regions, max_regions);
    // 開機磁區、分頁表與核心映像都位於低位記憶體，必須排除在可用記憶體之外。
    // The boot sector, the page tables, and the kernel image all sit in low
    // memory and must be kept out of usable memory.
    const auto kernel_end = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(__kernel_end));
    firmware::append(regions, count, max_regions, 0, kernel_end, MemoryType::Reserved);

    boot_info.version = BootInfo::current_version;
    boot_info.memory_regions = regions;
    boot_info.memory_region_count = count;
    boot_info.framebuffer = {};
    boot_info.device_tree = nullptr;
    boot_info.device_tree_size = 0;
    boot_info.firmware_data = const_cast<void*>(static_cast<const void*>(table));
    boot_info.modules = nullptr;
    boot_info.module_count = 0;
    return &boot_info;
}
