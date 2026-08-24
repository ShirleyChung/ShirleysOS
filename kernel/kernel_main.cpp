#include "shirley/arch.hpp"
#include "shirley/boot_info.hpp"
#include "shirley/console.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform.hpp"

extern "C" [[noreturn]] void kernel_main(const shirley::BootInfo* boot_info) {
    shirley::arch::initialize();
    shirley::platform::initialize(*boot_info);
    shirley::console::initialize();
    shirley::memory::initialize(*boot_info);
    shirley::console::write("ShirleyOS booting...\n");
    shirley::console::write("Memory manager initialized.\n");
    shirley::console::write("Userspace initialized.\n");
    shirley::console::write("Hello! Shirley's OS.\n");
    shirley::arch::halt();
}
