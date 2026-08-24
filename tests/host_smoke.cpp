#include "shirley/boot_info.hpp"
#include "shirley/memory.hpp"
#include <cassert>

int main() {
    // 建立含有三個可用頁面的測試記憶體區段。
    shirley::BootMemoryRegion regions[] = {{0x1000, 3 * shirley::memory::page_size, shirley::MemoryType::Usable}};
    shirley::BootInfo info{shirley::BootInfo::current_version, regions, 1};
    shirley::memory::initialize(info);
    // 確認分頁器統計符合開機資訊。
    assert(shirley::memory::total_pages() == 3);
    assert(shirley::memory::free_pages() == 3);
    return 0;
}
