#include "shirley/boot_info.hpp"
#include "shirley/memory.hpp"
#include <cassert>

int main() {
    shirley::BootMemoryRegion regions[] = {{0x1000, 3 * shirley::memory::page_size, shirley::MemoryType::Usable}};
    shirley::BootInfo info{shirley::BootInfo::current_version, regions, 1};
    shirley::memory::initialize(info);
    assert(shirley::memory::total_pages() == 3);
    assert(shirley::memory::free_pages() == 3);
    return 0;
}
