#pragma once

#include "shirley/boot_info.hpp"
#include <cstddef>
#include <cstdint>

namespace shirley::memory {

using PhysicalAddress = std::uint64_t;
constexpr std::size_t page_size = 4096;

void initialize(const BootInfo&);
PhysicalAddress allocate_page();
void free_page(PhysicalAddress);
std::size_t total_pages();
std::size_t free_pages();

} // namespace shirley::memory
