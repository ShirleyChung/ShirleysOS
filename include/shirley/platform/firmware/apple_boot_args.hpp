#pragma once

#include "shirley/boot_info.hpp"

// Apple Silicon 的 iBoot 不使用裝置樹二進位格式，而是把一個 boot_args 結構的
// 位址放在 x0。m1n1 之類的中介載入器會沿用同一份結構，因此 ShirleyOS 直接讀它。
// 欄位配置取自 Apple 公開的 XNU 原始碼（pexpert/pexpert/arm64/boot.h）。
//
// Apple Silicon's iBoot does not use the flattened device tree format; it puts
// the address of a boot_args structure in x0. Intermediate loaders such as
// m1n1 keep the same structure, so ShirleyOS reads it directly. The field
// layout comes from Apple's published XNU sources
// (pexpert/pexpert/arm64/boot.h).
namespace shirley::platform::firmware {

// 版本 2 之後的欄位配置維持相容；再舊的版本不支援。
// Revision 2 and later share a compatible layout; older ones are unsupported.
constexpr std::uint16_t apple_boot_args_minimum_revision = 2;

struct AppleVideoInfo {
    std::uint64_t base_address;
    std::uint64_t display;
    // 每一列的位元組數 / bytes per scanline
    std::uint64_t stride;
    std::uint64_t width;
    std::uint64_t height;
    // 低 16 位元為每像素位元數 / low 16 bits are bits per pixel
    std::uint64_t depth;
};

struct AppleBootArgs {
    std::uint16_t revision;
    std::uint16_t version;
    std::uint64_t virtual_base;
    std::uint64_t physical_base;
    std::uint64_t memory_size;
    std::uint64_t top_of_kernel_data;
    AppleVideoInfo video;
    std::uint32_t machine_type;
    std::uint32_t padding;
    // Apple 自有的 device tree 格式 / Apple's own device tree format
    std::uint64_t device_tree;
    std::uint32_t device_tree_size;
    char command_line[256];
    std::uint64_t boot_flags;
    std::uint64_t memory_size_actual;
};

// 確認指標指向可用的 boot_args。
// Check that the pointer refers to usable boot_args.
bool apple_boot_args_valid(const void* arguments);
// 讀出可用記憶體與韌體已使用的區段，回傳寫入的區段數。
// Read out usable memory and the ranges firmware already occupies, returning
// how many regions were written.
std::uint64_t apple_boot_args_memory_map(const void* arguments, BootMemoryRegion* regions,
                                         std::uint64_t capacity);
// 讀出 framebuffer 描述；沒有顯示輸出時回傳 false。
// Read the framebuffer description; returns false when there is no display.
bool apple_boot_args_framebuffer(const void* arguments, FramebufferInfo& framebuffer);

} // namespace shirley::platform::firmware
