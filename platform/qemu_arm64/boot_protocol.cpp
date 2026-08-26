#include "shirley/boot_info.hpp"
#include "shirley/platform/firmware/fdt.hpp"

// 由連結腳本提供，標示核心映像在實體記憶體中的範圍。
// Provided by the linker script: where the kernel image starts and ends in
// physical memory.
extern "C" const char __kernel_start[];
extern "C" const char __kernel_end[];

namespace shirley::platform {
namespace {

// QEMU virt 的 RAM 固定從 1 GiB 開始；韌體沒有提供裝置樹時使用的保守預設值。
// RAM on QEMU virt always starts at 1 GiB. These conservative defaults are
// only used when firmware supplies no device tree.
constexpr std::uint64_t fallback_ram_base = 0x40000000ull;
constexpr std::uint64_t fallback_ram_size = 128ull * 1024 * 1024;
constexpr std::uint64_t max_regions = 32;

BootMemoryRegion regions[max_regions];
BootInfo boot_info;

bool append(std::uint64_t& count, std::uint64_t start, std::uint64_t length, MemoryType type) {
    if (length == 0 || count >= max_regions) return false;
    regions[count++] = {start, length, type};
    return true;
}

} // namespace
} // namespace shirley::platform

// 由 arch/arm64/entry.S 呼叫；QEMU virt 以 x0 傳入裝置樹位址。
// Called from arch/arm64/entry.S; QEMU virt passes the device tree address in
// x0.
extern "C" const shirley::BootInfo* shirley_platform_boot_info(const void* firmware_table) {
    using namespace shirley;
    using namespace shirley::platform;

    std::uint64_t count = 0;
    const bool has_device_tree = firmware::fdt_valid(firmware_table);
    if (has_device_tree) count = firmware::fdt_memory_map(firmware_table, regions, max_regions);
    // 裝置樹缺席或沒有 /memory 節點時，退回 QEMU virt 的預設 RAM 配置。
    // If the device tree is missing or has no /memory node, fall back to the
    // default QEMU virt RAM layout.
    if (count == 0) append(count, fallback_ram_base, fallback_ram_size, MemoryType::Usable);

    // 核心映像與裝置樹本身都必須排除在可用記憶體之外。
    // Both the kernel image and the device tree itself must be kept out of
    // usable memory.
    const auto kernel_start = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(__kernel_start));
    const auto kernel_end = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(__kernel_end));
    append(count, kernel_start, kernel_end - kernel_start, MemoryType::Reserved);
    if (has_device_tree) {
        append(count, static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(firmware_table)),
               firmware::fdt_total_size(firmware_table), MemoryType::Firmware);
    }

    boot_info.version = BootInfo::current_version;
    boot_info.memory_regions = regions;
    boot_info.memory_region_count = count;
    boot_info.framebuffer = {};
    boot_info.device_tree = has_device_tree ? const_cast<void*>(firmware_table) : nullptr;
    boot_info.device_tree_size = has_device_tree ? firmware::fdt_total_size(firmware_table) : 0;
    boot_info.firmware_data = const_cast<void*>(firmware_table);
    boot_info.modules = nullptr;
    boot_info.module_count = 0;
    return &boot_info;
}
