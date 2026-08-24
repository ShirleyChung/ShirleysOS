#include "shirley/memory.hpp"

namespace shirley::memory {
// 目前以計數器記錄總頁數與可用頁數，實際頁面佈局尚待實作。
namespace { std::size_t total = 0; std::size_t available = 0; }

void initialize(const BootInfo& info) {
    // 每次初始化先清除舊統計，只計入可用且完整對齊的頁面。
    total = available = 0;
    for (std::uint64_t i = 0; i < info.memory_region_count; ++i) {
        const auto& region = info.memory_regions[i];
        if (region.type != MemoryType::Usable) continue;
        const auto pages = static_cast<std::size_t>(region.length / page_size);
        total += pages;
        available += pages;
    }
}
// 頁面分配介面目前是 stub；真正分配器完成前回傳 0。
PhysicalAddress allocate_page() { return 0; }
// 頁面釋放介面目前是 stub。
void free_page(PhysicalAddress) {}
std::size_t total_pages() { return total; }
std::size_t free_pages() { return available; }
} // namespace shirley::memory
