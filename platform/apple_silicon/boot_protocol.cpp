#include "shirley/boot_info.hpp"
#include "shirley/platform/firmware/apple_boot_args.hpp"

// 由連結腳本提供，標示核心映像在實體記憶體中的範圍。
// Provided by the linker script: where the kernel image starts and ends in
// physical memory.
extern "C" const char __kernel_start[];
extern "C" const char __kernel_end[];

namespace shirley::platform {
namespace {

constexpr std::uint64_t max_regions = 16;

BootMemoryRegion regions[max_regions];
BootInfo boot_info;

bool append(std::uint64_t& count, std::uint64_t start, std::uint64_t length, MemoryType type) {
    if (length == 0 || count >= max_regions) return false;
    regions[count++] = {start, length, type};
    return true;
}

} // namespace
} // namespace shirley::platform

// 由 arch/arm64/entry.S 呼叫；iBoot 與 m1n1 都以 x0 傳入 boot_args 位址。
// Called from arch/arm64/entry.S; both iBoot and m1n1 pass the boot_args
// address in x0.
extern "C" const shirley::BootInfo* shirley_platform_boot_info(const void* firmware_table) {
    using namespace shirley;
    using namespace shirley::platform;

    std::uint64_t count = 0;
    const bool has_boot_args = firmware::apple_boot_args_valid(firmware_table);
    if (has_boot_args) count = firmware::apple_boot_args_memory_map(firmware_table, regions, max_regions);

    // 核心映像本身一律要排除在可用記憶體之外。
    // The kernel image itself is always kept out of usable memory.
    const auto kernel_start = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(__kernel_start));
    const auto kernel_end = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(__kernel_end));
    append(count, kernel_start, kernel_end - kernel_start, MemoryType::Reserved);

    boot_info.version = BootInfo::current_version;
    boot_info.memory_regions = regions;
    boot_info.memory_region_count = count;
    boot_info.framebuffer = {};
    if (has_boot_args) firmware::apple_boot_args_framebuffer(firmware_table, boot_info.framebuffer);
    // Apple 的 device tree 不是 flattened device tree，格式解析排在 M8。
    // Apple's device tree is not a flattened device tree; parsing that format
    // arrives in M8.
    boot_info.device_tree = nullptr;
    boot_info.device_tree_size = 0;
    boot_info.firmware_data = const_cast<void*>(firmware_table);
    boot_info.modules = nullptr;
    boot_info.module_count = 0;
    return &boot_info;
}
