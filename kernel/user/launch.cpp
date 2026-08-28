#include "shirley/user_loader.hpp"

#include "shirley/arch.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform.hpp"

#if defined(SHIRLEY_ARCH_X86_64)
#include "shirley/arch/x86_64/paging.hpp"
#elif defined(SHIRLEY_ARCH_ARM64)
#include "shirley/arch/arm64/paging.hpp"
#include "internal.hpp"
#endif

extern "C" const char __kernel_start[];
extern "C" const char __kernel_end[];

namespace shirley::user {
namespace {

// 把平台列出的裝置記憶體映射進來。核心進入 user 空間之後並沒有停止使用
// 硬體：中斷隨時會送達，處理常式要讀中斷控制器，主控台還要能輸出。這些
// MMIO 不在 user 的分頁表裡的話，第一個在 user 空間收到的中斷就會變成一次
// data abort。
//
// Map the device memory the platform lists. The kernel does not stop using
// hardware once userspace is running: an interrupt can arrive at any moment,
// its handler reads the interrupt controller, and the console still prints.
// Without that MMIO in the user page tables, the first interrupt taken in
// userspace becomes a data abort.
bool map_platform_devices(memory::AddressSpace& space) {
    for (std::size_t index = 0; index < platform::mmio_region_count(); ++index) {
        const auto region = platform::mmio_region(index);
        if (region.bytes == 0) continue;
        const auto first = region.base & ~(memory::page_size - 1);
        const auto last = (region.base + region.bytes + memory::page_size - 1) &
                          ~(memory::page_size - 1);
        for (auto address = first; address < last; address += memory::page_size) {
            if (!space.map(address, address, memory::PageFlags::Read | memory::PageFlags::Write |
                                                 memory::PageFlags::Device))
                return false;
        }
    }
    return true;
}

bool map_kernel(memory::AddressSpace& space) {
    const auto begin = reinterpret_cast<std::uintptr_t>(__kernel_start) &
                       ~(memory::page_size - 1);
    const auto end_raw = reinterpret_cast<std::uintptr_t>(__kernel_end);
    const auto end = (end_raw + memory::page_size - 1) & ~(memory::page_size - 1);
    for (auto address = begin; address < end; address += memory::page_size) {
        if (!space.map(address, address, memory::PageFlags::Read | memory::PageFlags::Write |
                                      memory::PageFlags::Execute))
            return false;
    }
    return true;
}

} // namespace

bool launch_embedded() {
#if defined(SHIRLEY_ARCH_X86_64)
    arch::x86_64::PageTable address_space;
    if (!address_space.initialize() || !map_kernel(address_space) ||
        !map_platform_devices(address_space)) return false;
    // Ring 3 -> Ring 0 interrupt entry loads RSP0 from the TSS. The BIOS
    // entry path's kernel stack lives at 0x80000, outside the kernel image.
    if (!address_space.map(0x80000, 0x80000, memory::PageFlags::Read |
                                                     memory::PageFlags::Write |
                                                     memory::PageFlags::Execute)) return false;
    arch::set_kernel_stack(0x81000);
    Image image{};
    if (!load_elf(embedded_image(), embedded_image_size(), shirley::boot::elf_machine_x86_64,
                  address_space, image)) return false;
    arch::switch_address_space(address_space.root());
    arch::enter_userspace(image.entry, image.stack);
#elif defined(SHIRLEY_ARCH_ARM64)
    arch::arm64::PageTable address_space;
    if (!address_space.initialize() || !map_kernel(address_space) ||
        !map_platform_devices(address_space)) return false;
    Image image{};
    if (!load_elf(embedded_image(), embedded_image_size(), shirley::boot::elf_machine_aarch64,
                  address_space, image)) return false;
    if (!arch::arm64::mmu_enable(address_space.root())) return false;
    arch::enter_userspace(image.entry, image.stack);
#else
    return false;
#endif
}

} // namespace shirley::user
