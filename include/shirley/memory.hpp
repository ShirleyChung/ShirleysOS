#pragma once

#include "shirley/boot_info.hpp"
#include <cstddef>
#include <cstdint>

namespace shirley::memory {

// 實體位址的統一表示型別。
using PhysicalAddress = std::uint64_t;
// 記憶體分頁的固定大小。
constexpr std::size_t page_size = 4096;

// 以開機資訊初始化頁面分配器。
void initialize(const BootInfo&);
// 分配或釋放一個實體頁面。
PhysicalAddress allocate_page();
void free_page(PhysicalAddress);
std::size_t total_pages();
std::size_t free_pages();

} // namespace shirley::memory
