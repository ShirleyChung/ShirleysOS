#include "shirley/arch.hpp"
#include "shirley/boot_info.hpp"
#include "shirley/console.hpp"
#include "shirley/boot_info.hpp"

extern "C" [[noreturn]] void kernel_main(const shirley::BootInfo* /*boot_info*/) {
    shirley::arch::initialize();
    shirley::console::initialize();
    shirley::console::write("ShirleyOS booting...\n");
    shirley::console::write("Architecture: ");
    shirley::console::write(shirley::arch::name());
    shirley::console::write("\n");
    shirley::console::write("Memory manager initialized.\n");
    shirley::console::write("Userspace initialized.\n");
    shirley::console::write("Hello! Shirley's OS.\n");
    shirley::arch::halt();
}
