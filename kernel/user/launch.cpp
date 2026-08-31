#include "shirley/user_loader.hpp"

#include "shirley/arch.hpp"
#include "shirley/console.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform.hpp"
#include "shirley/vfs.hpp"

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

// 正在載入的映像暫放在這裡。放在 .bss 而不是堆疊或堆積：核心沒有堆積，而
// 這份緩衝區遠大於任何一個核心堆疊。因為是 .bss，它不佔核心映像的任何一個
// 位元組。一次只載入一個程式，所以一份就夠——等到有行程之後，這個假設要跟著
// 重新檢視。
//
// Where an image being loaded is held. It lives in .bss rather than on a stack
// or a heap: the kernel has no heap, and this buffer is far larger than any
// kernel stack. Being .bss, it takes up not one byte of the kernel image. One
// program is loaded at a time, so one buffer is enough; that assumption needs
// revisiting once there are processes.
unsigned char image_buffer[max_image_bytes];

void report(const char* path, const char* reason) {
    console::write(path);
    console::write(": ");
    console::write(reason);
    console::write("\n");
}

// 把路徑上的檔案讀進緩衝區，並在失敗時說清楚原因。
// Read the file at a path into the buffer, and say why when it does not work.
std::size_t read_image(const char* path) {
    const auto descriptor = vfs::open(path);
    if (descriptor < 0) {
        report(path, vfs::error_text(descriptor));
        return 0;
    }
    const auto* node = vfs::node_of(descriptor);
    const auto size = node != nullptr ? node->size : 0;
    vfs::close(descriptor);
    // 先問大小再讀，錯誤訊息才能說出真正的原因。直接讀的話，太大的程式只會
    // 表現成一份讀不完的檔案，看起來像 I/O 壞掉。
    //
    // Ask for the size before reading so the message can name the real reason.
    // Reading first would make an over-large program look like a file that
    // could not be finished, which reads as broken I/O.
    if (size > max_image_bytes) {
        report(path, "program is larger than the loader's buffer");
        return 0;
    }
    const auto length = vfs::read_file(path, image_buffer, sizeof(image_buffer));
    if (length == 0) {
        report(path, "could not be read");
        return 0;
    }
    return length;
}

} // namespace

bool launch(const char* path) {
    if (path == nullptr) return false;
    const auto size = read_image(path);
    if (size == 0) return false;
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
    if (!load_elf(image_buffer, size, shirley::boot::elf_machine_x86_64, address_space, image))
        return false;
    arch::switch_address_space(address_space.root());
    arch::enter_userspace(image.entry, image.stack);
#elif defined(SHIRLEY_ARCH_ARM64)
    arch::arm64::PageTable address_space;
    if (!address_space.initialize() || !map_kernel(address_space) ||
        !map_platform_devices(address_space)) return false;
    Image image{};
    if (!load_elf(image_buffer, size, shirley::boot::elf_machine_aarch64, address_space, image))
        return false;
    if (!arch::arm64::mmu_enable(address_space.root())) return false;
    arch::enter_userspace(image.entry, image.stack);
#else
    (void)size;
    return false;
#endif
}

} // namespace shirley::user
