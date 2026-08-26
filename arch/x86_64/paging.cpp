#include "shirley/arch/x86_64/paging.hpp"

#include "internal.hpp"
#include "shirley/memory.hpp"

namespace shirley::arch::x86_64 {
namespace {

constexpr std::uint64_t entry_present = 1ull << 0;
constexpr std::uint64_t entry_writable = 1ull << 1;
constexpr std::uint64_t entry_user = 1ull << 2;
constexpr std::uint64_t entry_huge = 1ull << 7;
constexpr std::uint64_t entry_no_execute = 1ull << 63;
// 分頁表項目中實體頁框位址的位元範圍。
// The bits of a page table entry that hold the physical frame address.
constexpr std::uint64_t address_mask = 0x000ffffffffff000ull;
constexpr std::size_t entries_per_table = 512;

// 低階實體記憶體為 identity mapping，因此實體位址可直接當成指標使用。
// Low physical memory is identity-mapped, so a physical address can be used
// as a pointer directly.
std::uint64_t* table_of(memory::PhysicalAddress physical) {
    return reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(physical));
}

void clear_table(memory::PhysicalAddress physical) {
    auto* table = table_of(physical);
    for (std::size_t i = 0; i < entries_per_table; ++i) table[i] = 0;
}

// level 3 對應 PML4，level 0 對應 PT。
// Level 3 is the PML4 and level 0 is the PT.
std::size_t index_of(memory::VirtualAddress address, unsigned level) {
    return static_cast<std::size_t>((address >> (12 + 9 * level)) & 0x1ff);
}

// 取得下一層分頁表；create 為真時在缺少時配置新表。
// Fetch the next level down, allocating it when create is set.
std::uint64_t* descend(std::uint64_t* table, std::size_t index, bool create) {
    auto& entry = table[index];
    if ((entry & entry_present) == 0) {
        if (!create) return nullptr;
        const auto page = memory::allocate_page();
        if (page == 0) return nullptr;
        clear_table(page);
        // 中間層一律允許寫入與使用者存取，實際權限由最後一層決定。
        // Intermediate levels always allow write and user access; the final
        // level decides the effective permissions.
        entry = (page & address_mask) | entry_present | entry_writable | entry_user;
    } else if ((entry & entry_huge) != 0) {
        // 已存在的大分頁不支援再細分。
        // Splitting an existing huge page is not supported.
        return nullptr;
    }
    return table_of(entry & address_mask);
}

std::uint64_t translate_flags(memory::PageFlags flags) {
    std::uint64_t value = entry_present;
    if (contains(flags, memory::PageFlags::Write)) value |= entry_writable;
    if (contains(flags, memory::PageFlags::User)) value |= entry_user;
    if (!contains(flags, memory::PageFlags::Execute) && cpu_nx_supported()) value |= entry_no_execute;
    return value;
}

// 釋放某一層以下的所有分頁表，但不碰被映射的資料頁。
// Free every page table at and below a level, leaving the mapped data pages
// untouched.
void free_level(memory::PhysicalAddress physical, unsigned level) {
    auto* table = table_of(physical);
    if (level > 0) {
        for (std::size_t i = 0; i < entries_per_table; ++i) {
            const auto entry = table[i];
            if ((entry & entry_present) == 0 || (entry & entry_huge) != 0) continue;
            free_level(entry & address_mask, level - 1);
        }
    }
    memory::free_page(physical);
}

} // namespace

PageTable::~PageTable() { destroy(); }

bool PageTable::initialize() {
    if (root_ != 0) return true;
    const auto page = memory::allocate_page();
    if (page == 0) return false;
    clear_table(page);
    root_ = page;
    return true;
}

void PageTable::destroy() {
    if (root_ == 0) return;
    // PML4 為 level 3，只釋放 PDPT/PD/PT 這三層與根層本身。
    // The PML4 is level 3; this frees the PDPT/PD/PT levels and the root.
    free_level(root_, 3);
    root_ = 0;
}

bool PageTable::map(memory::VirtualAddress virtual_address, memory::PhysicalAddress physical,
                    memory::PageFlags flags) {
    if (root_ == 0) return false;
    if ((virtual_address % memory::page_size) != 0 || (physical % memory::page_size) != 0) return false;
    auto* table = table_of(root_);
    for (unsigned level = 3; level > 0; --level) {
        table = descend(table, index_of(virtual_address, level), true);
        if (table == nullptr) return false;
    }
    table[index_of(virtual_address, 0)] = (physical & address_mask) | translate_flags(flags);
    // 只有目前正在使用的分頁表需要立即讓 TLB 失效。
    // Only the page table currently in use needs its TLB entry invalidated now.
    if (current_address_space() == static_cast<AddressSpaceHandle>(root_))
        asm volatile("invlpg (%0)" : : "r"(static_cast<std::uintptr_t>(virtual_address)) : "memory");
    return true;
}

bool PageTable::unmap(memory::VirtualAddress virtual_address) {
    if (root_ == 0 || (virtual_address % memory::page_size) != 0) return false;
    auto* table = table_of(root_);
    for (unsigned level = 3; level > 0; --level) {
        table = descend(table, index_of(virtual_address, level), false);
        if (table == nullptr) return false;
    }
    auto& entry = table[index_of(virtual_address, 0)];
    if ((entry & entry_present) == 0) return false;
    entry = 0;
    if (current_address_space() == static_cast<AddressSpaceHandle>(root_))
        asm volatile("invlpg (%0)" : : "r"(static_cast<std::uintptr_t>(virtual_address)) : "memory");
    return true;
}

memory::PhysicalAddress PageTable::translate(memory::VirtualAddress virtual_address) const {
    if (root_ == 0) return 0;
    auto* table = table_of(root_);
    for (unsigned level = 3; level > 0; --level) {
        const auto entry = table[index_of(virtual_address, level)];
        if ((entry & entry_present) == 0) return 0;
        if ((entry & entry_huge) != 0) {
            // 2 MiB 或 1 GiB 大分頁：以該層的頁框大小取偏移量。
            // A 2 MiB or 1 GiB huge page: take the offset within that level's
            // frame size.
            const std::uint64_t page_bytes = 1ull << (12 + 9 * level);
            return (entry & address_mask & ~(page_bytes - 1)) | (virtual_address & (page_bytes - 1));
        }
        table = table_of(entry & address_mask);
    }
    const auto entry = table[index_of(virtual_address, 0)];
    if ((entry & entry_present) == 0) return 0;
    return (entry & address_mask) | (virtual_address % memory::page_size);
}

} // namespace shirley::arch::x86_64
