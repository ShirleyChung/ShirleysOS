#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley {

// 描述開機載入器提供的實體記憶體區段用途。
enum class MemoryType : std::uint32_t {
    Reserved = 0,
    Usable = 1,
    Firmware = 2,
    Mmio = 3,
    Reclaimable = 4,
};

struct BootMemoryRegion {
    // 區段起點、長度與用途。
    std::uint64_t physical_start;
    std::uint64_t length;
    MemoryType type;
};

struct FramebufferInfo {
    // 顯示 framebuffer 的位址、尺寸、步幅與像素格式。
    std::uint64_t address = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch = 0;
    std::uint32_t format = 0;
};

// 開機載入器附帶的模組，例如 initramfs 或驅動程式。
struct BootModule { const char* name; std::uint64_t address; std::uint64_t size; };

struct BootInfo {
    // 啟動資訊的版本與記憶體地圖。
    static constexpr std::uint32_t current_version = 1;
    std::uint32_t version = current_version;
    BootMemoryRegion* memory_regions = nullptr;
    std::uint64_t memory_region_count = 0;
    // 可選的 framebuffer、裝置樹、韌體資料與附帶模組。
    FramebufferInfo framebuffer{};
    void* device_tree = nullptr;
    std::uint64_t device_tree_size = 0;
    void* firmware_data = nullptr;
    BootModule* modules = nullptr;
    std::uint64_t module_count = 0;
};

} // namespace shirley
