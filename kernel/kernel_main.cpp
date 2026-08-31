#include "shirley/arch.hpp"
#include "shirley/boot_info.hpp"
#include "shirley/console.hpp"
#include "shirley/device.hpp"
#include "shirley/format.hpp"
#include "shirley/fs.hpp"
#include "shirley/io.hpp"
#include "shirley/irq.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform.hpp"
#include "shirley/rootfs.hpp"
#include "shirley/scheduler.hpp"
#include "shirley/shell.hpp"
#include "shirley/vfs.hpp"

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
    // 裝置註冊表要在第一個裝置註冊之前清空，而主控台初始化時就會把自己登記
    // 進去，因此它排在主控台之前。
    //
    // The device registry has to be cleared before the first device registers,
    // and console initialization publishes the console itself, so it comes
    // first.
    shirley::device::initialize();
    shirley::console::initialize();
    shirley::io::initialize_console_streams();
    // /dev/null 不對應任何硬體，因此不必等平台初始化。
    // /dev/null corresponds to no hardware and so does not wait for the
    // platform to come up.
    shirley::device::null_initialize();
    // 主控台可用後的第一件事，就是回報架構向量表已經安裝。
    // The first thing worth reporting once the console works is that the
    // architecture's vector table is installed.
    shirley::console::write("[IRQ] ");
    shirley::console::write(shirley::arch::interrupt_table_name());
    shirley::console::write(" initialized\n");

    // IRQ 層必須在任何驅動程式註冊之前清空，而平台初始化就會帶起自己的
    // 裝置驅動程式，所以順序是 IRQ 層、平台、記憶體、排程器。
    //
    // The IRQ layer has to be cleared before any driver registers, and
    // platform initialization brings up the platform's own device drivers, so
    // the order is IRQ layer, platform, memory, scheduler.
    shirley::irq::initialize();
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

    // 中斷向量已就緒，平台中斷控制器也只解除了已註冊驅動程式的那幾條線，
    // 因此在這裡開啟中斷是安全的。
    //
    // The vectors are installed and the platform controller has unmasked only
    // the lines whose drivers registered, so enabling interrupts here is safe.
    const auto timer_rate = shirley::platform::timer_frequency();
    shirley::arch::enable_interrupts();
    write_field("Interrupts: ", shirley::arch::interrupts_enabled() ? "enabled" : "disabled");
    if (timer_rate != 0) write_count("Timer: ", timer_rate, " Hz\n");

    // 根檔案系統是嵌在核心映像裡的唯讀映像，透過 RAM disk 掛載，因此開機
    // 不需要任何磁碟驅動程式就有檔案可讀。
    //
    // The root file system is a read-only image embedded in the kernel and
    // mounted through a RAM disk, so boot needs no disk driver to have files
    // to read.
    shirley::vfs::initialize();
    if (shirley::fs::mount_rootfs() &&
        shirley::vfs::mount("/", shirley::fs::shrfs_filesystem())) {
        write_count("Root file system: ", shirley::fs::entry_count(), " entries, ");
        write_count("", shirley::fs::total_file_bytes(), " bytes\n");
    } else {
        shirley::console::write("Root file system: mount failed\n");
    }
    // devfs 把裝置註冊表掛成 /dev。它必須排在根檔案系統之後：掛載點得先存在，
    // 而 /dev 這個目錄是根檔案系統提供的。
    //
    // devfs mounts the device registry at /dev. It has to come after the root
    // file system, because a mount point has to exist first and the /dev
    // directory is the root file system's.
    if (!shirley::vfs::mount("/dev", shirley::vfs::devfs()))
        shirley::console::write("/dev: mount failed\n");

    // 最後才列出裝置：平台的驅動程式與根檔案系統的磁碟都已經登記完，因此
    // 這份清單就是 /dev 底下實際會看到的內容。列出來的同時也是一次自我檢查
    // ——少了 kbd0 就代表鍵盤沒有上線。
    //
    // The devices are listed last: the platform's drivers and the root file
    // system's disk have all registered by now, so this list is exactly what
    // appears under /dev. Printing it doubles as a self-check: a missing kbd0
    // means the keyboard never came online.
    write_count("Devices: ", shirley::device::count(), "\n");
    shirley::device::dump();
    shirley::console::write("\n");

    // 開機到此結束，機器接下來就是這個提示符：shell 讀取中斷送進來的按鍵，
    // 沒有輸入時停在等待中斷的低功耗狀態，不輪詢任何裝置。
    //
    // Boot ends here and the machine becomes this prompt. The shell reads the
    // keystrokes interrupts deliver and, with nothing to read, parks in a
    // low-power wait rather than polling any device.
    shirley::shell::run();
}
