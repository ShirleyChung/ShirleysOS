#include "shirley/memory.hpp"

namespace shirley::memory {
namespace { std::size_t total = 0; std::size_t available = 0; }

void initialize(const BootInfo& info) {
    total = available = 0;
    for (std::uint64_t i = 0; i < info.memory_region_count; ++i) {
        const auto& region = info.memory_regions[i];
        if (region.type != MemoryType::Usable) continue;
        const auto pages = static_cast<std::size_t>(region.length / page_size);
        total += pages;
        available += pages;
    }
}
PhysicalAddress allocate_page() { return 0; }
void free_page(PhysicalAddress) {}
std::size_t total_pages() { return total; }
std::size_t free_pages() { return available; }
} // namespace shirley::memory
