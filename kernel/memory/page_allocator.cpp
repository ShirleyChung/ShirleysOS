#include "shirley/memory.hpp"

#include <array>
#include <limits>

namespace shirley::memory {
namespace {

struct Extent { PhysicalAddress begin = 0; PhysicalAddress end = 0; };
constexpr std::size_t max_extents = 4096;
std::array<Extent, max_extents> managed{};
std::array<Extent, max_extents> free_extents{};
std::size_t managed_count = 0;
std::size_t free_count = 0;
std::size_t total = 0;
std::size_t available = 0;

constexpr PhysicalAddress align_down(PhysicalAddress value) {
    return value & ~(static_cast<PhysicalAddress>(page_size) - 1);
}

bool align_usable(const BootMemoryRegion& region, Extent& result) {
    if (region.length == 0 ||
        region.physical_start > std::numeric_limits<PhysicalAddress>::max() - region.length ||
        region.physical_start > std::numeric_limits<PhysicalAddress>::max() - (page_size - 1)) return false;
    auto begin = align_down(region.physical_start + page_size - 1);
    const auto end = align_down(region.physical_start + region.length);
    if (begin == 0) begin = page_size; // Zero is the failure sentinel.
    if (begin >= end) return false;
    result = {begin, end};
    return true;
}

bool align_reserved(const BootMemoryRegion& region, Extent& result) {
    if (region.length == 0 ||
        region.physical_start > std::numeric_limits<PhysicalAddress>::max() - region.length) return false;
    const auto begin = align_down(region.physical_start);
    const auto raw_end = region.physical_start + region.length;
    PhysicalAddress end = raw_end;
    if ((raw_end & (page_size - 1)) != 0) {
        end = raw_end > std::numeric_limits<PhysicalAddress>::max() - (page_size - 1)
            ? std::numeric_limits<PhysicalAddress>::max()
            : align_down(raw_end + page_size - 1);
    }
    if (begin >= end) return false;
    result = {begin, end};
    return true;
}

bool insert_extent(std::array<Extent, max_extents>& extents, std::size_t& count, Extent added) {
    std::size_t first = 0;
    while (first < count && extents[first].end < added.begin) ++first;
    std::size_t last = first;
    while (last < count && extents[last].begin <= added.end) {
        if (extents[last].begin < added.begin) added.begin = extents[last].begin;
        if (extents[last].end > added.end) added.end = extents[last].end;
        ++last;
    }
    const auto removed = last - first;
    if (removed == 0 && count == max_extents) return false;
    for (std::size_t i = count; i > last; --i) extents[i - removed] = extents[i - 1];
    extents[first] = added;
    count = count - removed + 1;
    return true;
}

void subtract_extent(std::array<Extent, max_extents>& extents, std::size_t& count, Extent removed) {
    for (std::size_t i = 0; i < count;) {
        auto& current = extents[i];
        if (removed.end <= current.begin || removed.begin >= current.end) { ++i; continue; }
        if (removed.begin <= current.begin && removed.end >= current.end) {
            for (std::size_t j = i + 1; j < count; ++j) extents[j - 1] = extents[j];
            --count;
            continue;
        }
        if (removed.begin <= current.begin) { current.begin = removed.end; ++i; continue; }
        if (removed.end >= current.end) { current.end = removed.begin; ++i; continue; }
        const auto old_end = current.end;
        current.end = removed.begin;
        // If metadata is exhausted, conservatively discard the upper half.
        if (count < max_extents) {
            for (std::size_t j = count; j > i + 1; --j) extents[j] = extents[j - 1];
            extents[i + 1] = {removed.end, old_end};
            ++count;
        }
        ++i;
    }
}

std::size_t count_pages(const std::array<Extent, max_extents>& extents, std::size_t count) {
    std::size_t pages = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto extent_pages = (extents[i].end - extents[i].begin) / page_size;
        if (extent_pages > std::numeric_limits<std::size_t>::max() - pages)
            return std::numeric_limits<std::size_t>::max();
        pages += static_cast<std::size_t>(extent_pages);
    }
    return pages;
}

bool contains(const std::array<Extent, max_extents>& extents, std::size_t count, PhysicalAddress address) {
    for (std::size_t i = 0; i < count; ++i) {
        if (address >= extents[i].begin && address < extents[i].end) return true;
        if (address < extents[i].begin) break;
    }
    return false;
}

} // namespace

void initialize(const BootInfo& info) {
    managed_count = free_count = total = available = 0;
    if (info.version != BootInfo::current_version ||
        (info.memory_region_count != 0 && info.memory_regions == nullptr)) return;
    for (std::uint64_t i = 0; i < info.memory_region_count; ++i) {
        Extent extent{};
        if (info.memory_regions[i].type == MemoryType::Usable && align_usable(info.memory_regions[i], extent))
            insert_extent(managed, managed_count, extent);
    }
    // Non-usable descriptors win if firmware supplies overlapping entries.
    for (std::uint64_t i = 0; i < info.memory_region_count; ++i) {
        Extent extent{};
        if (info.memory_regions[i].type != MemoryType::Usable && align_reserved(info.memory_regions[i], extent))
            subtract_extent(managed, managed_count, extent);
    }
    free_count = managed_count;
    for (std::size_t i = 0; i < managed_count; ++i) free_extents[i] = managed[i];
    total = available = count_pages(managed, managed_count);
}

PhysicalAddress allocate_page() {
    if (free_count == 0) return 0;
    const auto result = free_extents[0].begin;
    free_extents[0].begin += page_size;
    if (free_extents[0].begin == free_extents[0].end) {
        for (std::size_t i = 1; i < free_count; ++i) free_extents[i - 1] = free_extents[i];
        --free_count;
    }
    --available;
    return result;
}

void free_page(PhysicalAddress address) {
    if ((address & (page_size - 1)) != 0 ||
        !contains(managed, managed_count, address) || contains(free_extents, free_count, address)) return;
    if (insert_extent(free_extents, free_count, {address, address + page_size})) ++available;
}
std::size_t total_pages() { return total; }
std::size_t free_pages() { return available; }
} // namespace shirley::memory
