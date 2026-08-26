#include "shirley/arch.hpp"
#include "shirley/boot_info.hpp"
#include "shirley/console.hpp"
#include "shirley/format.hpp"
#include "shirley/io.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform.hpp"
#include "shirley/scheduler.hpp"

namespace {

// 開機載入器未提供啟動資訊時使用的空白描述。
// Stands in when the boot loader supplied no boot information.
constexpr shirley::BootInfo empty_boot_info{};

void write_field(const char* label, const char* value) {
    shirley::console::write(label);
    shirley::console::write(value);
    shirley::console::write("\n");
}

void write_count(const char* label, std::uint64_t value, const char* suffix) {
    // 64 位元十進位最多 20 位數，加上結尾。
    // A 64-bit decimal value needs at most 20 digits, plus the terminator.
    char digits[21];
    shirley::console::write(label);
    shirley::format::to_decimal(digits, sizeof(digits), value);
    shirley::console::write(digits);
    shirley::console::write(suffix);
}

} // namespace

// 核心的第一個 C++ 入口；boot_info 由平台的開機協定轉換而來。
// The kernel's first C++ entry point. boot_info comes from the platform's
// boot protocol.
extern "C" [[noreturn]] void kernel_main(const shirley::BootInfo* boot_info) {
    const auto& info = boot_info != nullptr ? *boot_info : empty_boot_info;

    // 先讓架構層可用，才能安裝例外處理並輸出診斷訊息。
    // Bring the architecture layer up first so exceptions are handled and
    // diagnostics can be printed.
    shirley::arch::initialize();
    shirley::console::initialize();
    shirley::io::initialize_console_streams();
    shirley::platform::initialize(info);
    shirley::memory::initialize(info);
    shirley::scheduler::initialize();

    shirley::console::write("ShirleyOS booting...\n");
    write_field("Architecture: ", shirley::arch::name());
    write_field("Processor: ", shirley::arch::cpu_vendor());
    write_field("Platform: ", shirley::platform::name());
    write_field("Machine: ", shirley::platform::machine());
    write_count("Memory regions: ", info.memory_region_count, "\n");
    write_count("Usable memory: ",
                static_cast<std::uint64_t>(shirley::memory::total_pages()) * shirley::memory::page_size /
                    (1024 * 1024),
                " MiB\n");
    write_count("Free pages: ", shirley::memory::free_pages(), "\n");

    // 中斷向量已就緒，且平台中斷控制器目前遮罩所有裝置中斷。
    // The vectors are installed and the platform controller currently masks
    // every device interrupt, so enabling interrupts here is safe.
    shirley::arch::enable_interrupts();
    write_field("Interrupts: ", shirley::arch::interrupts_enabled() ? "enabled" : "disabled");
    shirley::console::write("Hello! Shirley's OS.\n");

    // 尚未有可執行的工作，閒置等待下一個中斷。
    // There is nothing to run yet, so idle until the next interrupt.
    for (;;) shirley::arch::wait_for_interrupt();
}
