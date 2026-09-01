#include "shirley/user_loader.hpp"

#include "shirley/arch.hpp"
#include "shirley/console.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform.hpp"
#include "shirley/process.hpp"
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

// Keep allocator-owned RAM reachable by EL1/ring 0 while a process page table
// is active. The mappings deliberately omit User, so the process cannot touch
// physical memory. map_kernel() runs afterwards and restores executable flags
// on the kernel image where ranges overlap.
#if defined(SHIRLEY_ARCH_X86_64)
bool map_managed_memory(memory::AddressSpace& space) {
    for (std::size_t extent = 0; extent < memory::managed_extent_count(); ++extent) {
        for (auto address = memory::managed_extent_begin(extent);
             address < memory::managed_extent_end(extent); address += memory::page_size) {
            if (!space.map(address, address, memory::PageFlags::Read | memory::PageFlags::Write))
                return false;
        }
    }
    return true;
}
#endif

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

bool launch(const char* path, int* status) {
    if (path == nullptr) return false;
    const auto size = read_image(path);
    if (size == 0) return false;
    // 讓即將執行的程式一開始就有標準輸入／輸出／錯誤三個描述子。
    // Give the program about to run its standard input/output/error descriptors
    // from the outset.
    process::reset();
#if defined(SHIRLEY_ARCH_X86_64)
    arch::x86_64::PageTable address_space;
    if (!address_space.initialize() || !map_managed_memory(address_space) || !map_kernel(address_space) ||
        !map_platform_devices(address_space)) return false;
    Image image{};
    if (!load_elf(image_buffer, size, shirley::boot::elf_machine_x86_64, address_space, image))
        return false;
    // 切到程式的位址空間執行它，行程 exit 後再切回核心原本的位址空間，這樣
    // 接下來拆除 address_space 就不會動到仍在使用中的分頁表。
    //
    // Switch to the program's address space to run it, and switch back to the
    // kernel's own once the process exits, so tearing down address_space next
    // does not touch a page table still in use.
    const auto kernel_space = arch::current_address_space();
    arch::switch_address_space(address_space.root());
    const int code = arch::enter_userspace(image.entry, image.stack);
    arch::switch_address_space(kernel_space);
    if (status != nullptr) *status = code;
    return true;
#elif defined(SHIRLEY_ARCH_ARM64)
    // A child can be launched by /bin/init while its address space is active.
    // The page-table builder writes physical pages directly, so temporarily
    // return EL1 to the kernel's identity-mapped state while constructing it.
    const bool nested = arch::arm64::mmu_enabled();
    const auto previous_space = arch::current_address_space();
    if (nested) arch::arm64::mmu_disable();
    const auto restore_parent = [&]() {
        if (nested) arch::arm64::mmu_enable(previous_space);
    };
    arch::arm64::PageTable address_space;
    if (!address_space.initialize() || !map_kernel(address_space) ||
        !map_platform_devices(address_space)) { restore_parent(); return false; }
    Image image{};
    if (!load_elf(image_buffer, size, shirley::boot::elf_machine_aarch64, address_space, image))
        { restore_parent(); return false; }
    // 核心開機時 MMU 尚未啟用，因此這裡打開它並指向程式的轉換表；行程 exit
    // 後再關掉 MMU，回到核心無轉換的狀態，剛才的轉換表才能安全釋放。
    //
    // The kernel boots with the MMU off, so enable it here pointed at the
    // program's translation table; turn it off again once the process exits,
    // back to the kernel's no-translation state, so that table can be freed.
    if (!arch::arm64::mmu_enable(address_space.root())) { restore_parent(); return false; }
    const int code = arch::enter_userspace(image.entry, image.stack);
    arch::arm64::mmu_disable();
    // PageTable walks and frees physical table pages directly, so tear it down
    // before restoring the parent's translated address space.
    address_space.destroy();
    if (nested && !arch::arm64::mmu_enable(previous_space)) return false;
    if (status != nullptr) *status = code;
    return true;
#else
    (void)size;
    return false;
#endif
}

} // namespace shirley::user
