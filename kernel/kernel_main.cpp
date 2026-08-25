#include "shirley/arch.hpp"
#include "shirley/boot_info.hpp"
#include "shirley/console.hpp"

// 核心的第一個 C++ 入口；目前啟動資訊尚未被使用。
extern "C" [[noreturn]] void kernel_main(const shirley::BootInfo* /*boot_info*/) {
    // 依序初始化架構與主控台，再輸出啟動狀態。
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
