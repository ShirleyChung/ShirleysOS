#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley {

enum class MemoryType : std::uint32_t {
    Reserved = 0,
    Usable = 1,
    Firmware = 2,
    Mmio = 3,
    Reclaimable = 4,
};

struct BootMemoryRegion {
    std::uint64_t physical_start;
    std::uint64_t length;
    MemoryType type;
};

struct FramebufferInfo {
    std::uint64_t address = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch = 0;
    std::uint32_t format = 0;
};

struct BootModule { const char* name; std::uint64_t address; std::uint64_t size; };

struct BootInfo {
    static constexpr std::uint32_t current_version = 1;
    std::uint32_t version = current_version;
    BootMemoryRegion* memory_regions = nullptr;
    std::uint64_t memory_region_count = 0;
    FramebufferInfo framebuffer{};
    void* device_tree = nullptr;
    std::uint64_t device_tree_size = 0;
    void* firmware_data = nullptr;
    BootModule* modules = nullptr;
    std::uint64_t module_count = 0;
};

} // namespace shirley
