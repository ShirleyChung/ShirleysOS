#include "shirley/platform.hpp"

#include "internal.hpp"
#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"

namespace shirley::platform {
namespace {

Capabilities platform_capabilities{};

// M1（t8103）的固定周邊位址。實體機型會由 Apple device tree 提供，
// 解析該格式排在 M8；在此之前先以已知的預設值運作。
//
// Fixed peripheral addresses for M1 (t8103). On real hardware these come from
// the Apple device tree, whose format is parsed in M8; until then these known
// defaults are used.
constexpr std::uintptr_t t8103_aic_base = 0x23b100000ull;

} // namespace

void initialize(const BootInfo& boot_info) {
    apple::interrupt_controller_initialize(t8103_aic_base);
    platform_capabilities = {
        .serial_console = true,
        .interrupt_controller = apple::interrupt_controller_present(),
        // Apple 的系統計時器驅動程式排在 M2。
        // The Apple system timer driver arrives in M2.
        .timer = false,
        .framebuffer = boot_info.framebuffer.address != 0,
    };
}

const char* name() { return "Apple Silicon"; }
const char* machine() { return "Apple Silicon Mac with iBoot firmware"; }
const Capabilities& capabilities() { return platform_capabilities; }

void enable_irq(Irq irq) { apple::interrupt_controller_unmask(irq); }
void disable_irq(Irq irq) { apple::interrupt_controller_mask(irq); }
void end_of_interrupt(Irq irq) { apple::interrupt_controller_end_of_interrupt(irq); }
// AIC 的裝置中斷集中送到 IRQ 例外入口，由控制器驅動程式再分辨來源。
// AIC device interrupts all arrive at the IRQ exception entry, where the
// controller driver identifies the source.
unsigned irq_vector(Irq) { return arch::arm64::current_el_spx_irq; }

// Apple Silicon 沒有 PSCI；關機與重開機需要 SMC 與 PMU 驅動程式，排在 M8。
// Apple Silicon has no PSCI. Power off and restart need SMC and PMU drivers,
// which arrive in M8.
[[noreturn]] void power_off() { arch::halt(); }
[[noreturn]] void restart() { arch::halt(); }

} // namespace shirley::platform
