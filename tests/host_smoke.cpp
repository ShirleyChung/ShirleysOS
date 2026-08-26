#include "shirley/boot_info.hpp"
#include "shirley/memory.hpp"
#include <cassert>

int main() {
    using shirley::MemoryType;
    using shirley::memory::page_size;
    shirley::BootMemoryRegion regions[] = {
        {0x0000, 2 * page_size, MemoryType::Usable},
        {0x2001, 3 * page_size, MemoryType::Usable},
        {0x3000, page_size, MemoryType::Reserved},
        {0x5000, 2 * page_size, MemoryType::Usable},
        {0x6000, 2 * page_size, MemoryType::Usable},
    };
    shirley::BootInfo info{shirley::BootInfo::current_version, regions, 5};
    shirley::memory::initialize(info);
    assert(shirley::memory::total_pages() == 5);
    assert(shirley::memory::free_pages() == 5);
    const auto first = shirley::memory::allocate_page();
    const auto second = shirley::memory::allocate_page();
    assert(first == 0x1000);
    assert(second == 0x4000);
    assert(shirley::memory::allocate_page() == 0x5000);
    assert(shirley::memory::allocate_page() == 0x6000);
    assert(shirley::memory::allocate_page() == 0x7000);
    assert(shirley::memory::allocate_page() == 0);
    assert(shirley::memory::free_pages() == 0);
    shirley::memory::free_page(second);
    // 重複釋放、保留區段與未對齊的位址都必須被忽略。
    // A double free, a reserved range, and an unaligned address are all ignored.
    shirley::memory::free_page(second);
    shirley::memory::free_page(0x3000);
    shirley::memory::free_page(0x4001);
    assert(shirley::memory::free_pages() == 1);
    assert(shirley::memory::allocate_page() == second);
    assert(shirley::memory::free_pages() == 0);

    shirley::BootInfo invalid{};
    invalid.version = shirley::BootInfo::current_version + 1;
    shirley::memory::initialize(invalid);
    assert(shirley::memory::total_pages() == 0);
    assert(shirley::memory::allocate_page() == 0);
    return 0;
}
