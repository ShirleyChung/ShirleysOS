#pragma once

#include "shirley/boot_info.hpp"
#include <cstddef>
#include <cstdint>

namespace shirley::memory {

// 實體位址的統一表示型別。
// The single representation used for physical addresses.
using PhysicalAddress = std::uint64_t;
// 記憶體分頁的固定大小。
// The fixed page size.
constexpr std::size_t page_size = 4096;

// 以開機資訊初始化頁面分配器。
// Initialize the page allocator from the boot information.
void initialize(const BootInfo&);
// 分配或釋放一個實體頁面；分配失敗時回傳 0。
// Allocate or release one physical page; allocation returns 0 on failure.
PhysicalAddress allocate_page();
void free_page(PhysicalAddress);
std::size_t total_pages();
std::size_t free_pages();
// Physical extents owned by the allocator. Kernels map these supervisor-only
// into a process address space so page-table construction can access newly
// allocated pages while that process is active.
std::size_t managed_extent_count();
PhysicalAddress managed_extent_begin(std::size_t index);
PhysicalAddress managed_extent_end(std::size_t index);

} // namespace shirley::memory
