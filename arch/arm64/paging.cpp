#include "shirley/arch/arm64/paging.hpp"

#include "internal.hpp"
#include "shirley/memory.hpp"

namespace shirley::arch::arm64 {
namespace {

// 描述元最低兩位元決定型別：0b11 為表格或分頁，0b01 為區塊。
// The low two bits of a descriptor give its type: 0b11 is a table or page,
// 0b01 is a block.
constexpr std::uint64_t descriptor_valid = 1ull << 0;
constexpr std::uint64_t descriptor_table = 3ull;
constexpr std::uint64_t descriptor_page = 3ull;
constexpr std::uint64_t descriptor_block = 1ull;
constexpr std::uint64_t descriptor_type_mask = 3ull;
// 輸出位址欄位在描述元的位元 47:12。
// The output address occupies bits 47:12 of a descriptor.
constexpr std::uint64_t address_mask = 0x0000fffffffff000ull;

// MAIR 索引 1：一般記憶體 / MAIR index 1: normal memory
constexpr std::uint64_t attribute_index_normal = 1ull << 2;
constexpr std::uint64_t access_flag = 1ull << 10;
constexpr std::uint64_t inner_shareable = 3ull << 8;
constexpr std::uint64_t access_read_write_el1 = 0ull << 6;
constexpr std::uint64_t access_read_write_el0 = 1ull << 6;
constexpr std::uint64_t access_read_only_el1 = 2ull << 6;
constexpr std::uint64_t access_read_only_el0 = 3ull << 6;
constexpr std::uint64_t privileged_execute_never = 1ull << 53;
constexpr std::uint64_t user_execute_never = 1ull << 54;

constexpr std::size_t entries_per_table = 512;

// 走訪轉換表時直接以實體位址存取，見標頭檔的前提說明。
// Table walks dereference physical addresses directly; see the header for the
// precondition this relies on.
std::uint64_t* table_of(memory::PhysicalAddress physical) {
    return reinterpret_cast<std::uint64_t*>(static_cast<std::uintptr_t>(physical));
}

void clear_table(memory::PhysicalAddress physical) {
    auto* table = table_of(physical);
    for (std::size_t i = 0; i < entries_per_table; ++i) table[i] = 0;
}

// level 3 對應 L0，level 0 對應 L3。
// Level 3 is L0 and level 0 is L3.
std::size_t index_of(memory::VirtualAddress address, unsigned level) {
    return static_cast<std::size_t>((address >> (12 + 9 * level)) & 0x1ff);
}

// 取得下一層轉換表；create 為真時在缺少時配置新表。
// Fetch the next level down, allocating it when create is set.
std::uint64_t* descend(std::uint64_t* table, std::size_t index, bool create) {
    auto& entry = table[index];
    if ((entry & descriptor_valid) == 0) {
        if (!create) return nullptr;
        const auto page = memory::allocate_page();
        if (page == 0) return nullptr;
        clear_table(page);
        entry = (page & address_mask) | descriptor_table;
    } else if ((entry & descriptor_type_mask) == descriptor_block) {
        // 已存在的區塊映射不支援再細分。
        // Splitting an existing block mapping is not supported.
        return nullptr;
    }
    return table_of(entry & address_mask);
}

std::uint64_t translate_flags(memory::PageFlags flags) {
    const bool user = contains(flags, memory::PageFlags::User);
    const bool writable = contains(flags, memory::PageFlags::Write);
    std::uint64_t value = descriptor_page | access_flag | inner_shareable | attribute_index_normal;
    if (user)
        value |= writable ? access_read_write_el0 : access_read_only_el0;
    else
        value |= writable ? access_read_write_el1 : access_read_only_el1;
    // EL1 永遠不執行使用者頁面，EL0 也不執行核心頁面。
    // EL1 never executes user pages and EL0 never executes kernel pages.
    if (!contains(flags, memory::PageFlags::Execute)) value |= privileged_execute_never | user_execute_never;
    else if (user) value |= privileged_execute_never;
    else value |= user_execute_never;
    return value;
}

void invalidate(memory::VirtualAddress address) {
    // TLBI 的運算元是虛擬位址右移 12 位元後的頁碼。
    // The TLBI operand is the virtual address shifted right by 12, i.e. the
    // page number.
    asm volatile("dsb ishst; tlbi vaae1is, %0; dsb ish; isb" : : "r"(address >> 12) : "memory");
}

// 釋放某一層以下的所有轉換表，但不碰被映射的資料頁。
// Free every translation table at and below a level, leaving the mapped data
// pages untouched.
void free_level(memory::PhysicalAddress physical, unsigned level) {
    auto* table = table_of(physical);
    if (level > 0) {
        for (std::size_t i = 0; i < entries_per_table; ++i) {
            const auto entry = table[i];
            if ((entry & descriptor_type_mask) != descriptor_table) continue;
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
    // L0 為 level 3；釋放 L1/L2/L3 這三層與根層本身。
    // L0 is level 3; this frees the L1/L2/L3 levels and the root.
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
    invalidate(virtual_address);
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
    if ((entry & descriptor_valid) == 0) return false;
    entry = 0;
    invalidate(virtual_address);
    return true;
}

memory::PhysicalAddress PageTable::translate(memory::VirtualAddress virtual_address) const {
    if (root_ == 0) return 0;
    auto* table = table_of(root_);
    for (unsigned level = 3; level > 0; --level) {
        const auto entry = table[index_of(virtual_address, level)];
        if ((entry & descriptor_valid) == 0) return 0;
        if ((entry & descriptor_type_mask) == descriptor_block) {
            // 區塊映射：以該層的區塊大小取偏移量。
            // A block mapping: take the offset within that level's block size.
            const std::uint64_t block_bytes = 1ull << (12 + 9 * level);
            return (entry & address_mask & ~(block_bytes - 1)) | (virtual_address & (block_bytes - 1));
        }
        table = table_of(entry & address_mask);
    }
    const auto entry = table[index_of(virtual_address, 0)];
    if ((entry & descriptor_valid) == 0) return 0;
    return (entry & address_mask) | (virtual_address % memory::page_size);
}

bool mmu_enable(std::uint64_t translation_table) {
    if (translation_table == 0) return false;

    // MAIR 索引 0 為 Device-nGnRnE，索引 1 為一般的 write-back 快取記憶體。
    // MAIR index 0 is Device-nGnRnE and index 1 is normal write-back cacheable
    // memory.
    constexpr std::uint64_t mair = (0x00ull << 0) | (0xffull << 8);
    // TCR：T0SZ=16 代表 48 位元虛擬位址；TG0=4 KiB、內外皆 write-back、inner shareable。
    // TCR: T0SZ=16 gives 48-bit virtual addresses; TG0 is 4 KiB, inner and
    // outer caches are write-back, and the region is inner shareable.
    constexpr std::uint64_t tcr = 16ull | (1ull << 8) | (1ull << 10) | (3ull << 12) |
                                  (16ull << 16) | (1ull << 24) | (1ull << 26) | (3ull << 28) |
                                  (2ull << 30);
    std::uint64_t memory_features = 0;
    asm volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(memory_features));
    // IPS 直接沿用處理器回報的實體位址寬度。
    // IPS simply reuses the physical address width the processor reports.
    const std::uint64_t physical_range = (memory_features & 0xf) << 32;

    asm volatile("msr mair_el1, %0" : : "r"(mair));
    asm volatile("msr tcr_el1, %0" : : "r"(tcr | physical_range));
    asm volatile("msr ttbr0_el1, %0" : : "r"(translation_table));
    asm volatile("dsb ish; tlbi vmalle1; dsb ish; isb" : : : "memory");

    std::uint64_t control = 0;
    asm volatile("mrs %0, sctlr_el1" : "=r"(control));
    // M 啟用 MMU、C 啟用資料快取、I 啟用指令快取。
    // M enables the MMU, C the data cache, and I the instruction cache.
    control |= (1ull << 0) | (1ull << 2) | (1ull << 12);
    asm volatile("msr sctlr_el1, %0; isb" : : "r"(control) : "memory");
    return true;
}

} // namespace shirley::arch::arm64
