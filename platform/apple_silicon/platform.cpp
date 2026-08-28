#include "shirley/platform.hpp"

#include "internal.hpp"
#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"
#include "shirley/console.hpp"

namespace shirley::platform {
Capabilities platform_capabilities{};

namespace {

// M1（t8103）的固定周邊位址。實體機型會由 Apple device tree 提供，
// 解析該格式排在 M8；在此之前先以已知的預設值運作。
//
// Fixed peripheral addresses for M1 (t8103). On real hardware these come from
// the Apple device tree, whose format is parsed in M8; until then these known
// defaults are used.
constexpr std::uintptr_t t8103_aic_base = 0x23b100000ull;

} // namespace

void initialize(const BootInfo& boot_info) {
    const bool controller = apple::interrupt_controller_initialize(t8103_aic_base);
    console::write(controller ? "[IRQ] AIC initialized\n"
                              : "[IRQ] no AIC found; device interrupts stay masked\n");
    // 架構計時器是 CPU 自帶的，與 AIC 無關，但它的中斷仍然要經過 AIC 才會
    // 送到核心，因此同樣要等控制器就緒。Apple SoC 上架構計時器的中斷編號
    // 來自 Apple 裝置樹，解析那個格式排在 M8，因此這裡還不啟用計時器。
    //
    // The architected timer belongs to the CPU rather than to the AIC, but its
    // interrupt still reaches the core through the AIC, so it too waits for
    // the controller. Which interrupt number it uses on an Apple SoC comes
    // from the Apple device tree, whose format is parsed in M8, so the timer
    // is not brought up here yet.
    platform_capabilities.serial_console = true;
    platform_capabilities.interrupt_controller = controller;
    platform_capabilities.timer = false;
    platform_capabilities.framebuffer = boot_info.framebuffer.address != 0;
}

const char* name() { return "Apple Silicon"; }
const char* machine() { return "Apple Silicon Mac with iBoot firmware"; }
const Capabilities& capabilities() { return platform_capabilities; }

void enable_irq(Irq irq) { apple::interrupt_controller_unmask(irq); }
void disable_irq(Irq irq) { apple::interrupt_controller_mask(irq); }
void end_of_interrupt(Irq irq) { apple::interrupt_controller_end_of_interrupt(irq); }
// AIC 的裝置中斷全部集中送到同一個 IRQ 例外入口，由控制器驅動程式讀取
// EVENT 暫存器分辨來源，因此這裡沒有專屬向量可以回報。
//
// Every AIC device interrupt lands on the same IRQ exception entry, and the
// controller driver reads the EVENT register to identify the source, so there
// is no dedicated vector to report here.
unsigned irq_vector(Irq) { return demultiplexed_vector; }
// AIC 沒有 8259A 那種假中斷；沒有待處理事件時 EVENT 暫存器回報的是
// 「無事件」，而不是一個假的中斷編號。
//
// The AIC has no equivalent of the 8259A's spurious interrupt. With nothing
// pending, its EVENT register reports "no event" rather than a fake interrupt
// number.
bool spurious_interrupt(Irq) { return false; }

// Apple 的系統計時器驅動程式排在 M8。
// The Apple system timer driver arrives in M8.
std::uint64_t timer_ticks() { return 0; }
unsigned timer_frequency() { return 0; }

// 中斷送達時要讀 AIC，主控台要寫除錯 UART，兩者在 user 位址空間裡都必須
// 存在。UART 的位址是問出來的而不是寫死的：韌體資料解析完之後它可能已經被
// 換成另一個位址。
//
// An arriving interrupt reads the AIC and the console writes the debug UART,
// so both have to exist inside the user address space. The UART address is
// asked for rather than hard-coded here: it may have been switched to another
// one once the firmware data was parsed.
std::size_t mmio_region_count() { return 2; }

MmioRegion mmio_region(std::size_t index) {
    switch (index) {
        case 0: return {apple::console_uart_base(), 0x4000};
        case 1: return {t8103_aic_base, 0x8000};
        default: return {};
    }
}

// Apple Silicon 沒有 PSCI；關機與重開機需要 SMC 與 PMU 驅動程式，排在 M8。
// Apple Silicon has no PSCI. Power off and restart need SMC and PMU drivers,
// which arrive in M8.
[[noreturn]] void power_off() { arch::halt(); }
[[noreturn]] void restart() { arch::halt(); }

} // namespace shirley::platform
