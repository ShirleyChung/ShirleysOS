#include "shirley/platform.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"

namespace shirley::platform {
namespace {

Capabilities platform_capabilities{};

// QEMU virt 的 PSCI 介面預設使用 HVC 呼叫慣例。
// The PSCI interface on QEMU virt uses the HVC calling convention by default.
constexpr std::uint32_t psci_system_off = 0x84000008;
constexpr std::uint32_t psci_system_reset = 0x84000009;

void psci_call(std::uint32_t function) {
    register std::uint64_t x0 asm("x0") = function;
    asm volatile("hvc #0" : "+r"(x0) : : "x1", "x2", "x3", "memory");
}

} // namespace

void initialize(const BootInfo& boot_info) {
    platform_capabilities = {
        .serial_console = true,
        // GICv2 驅動程式在 M2 才會加入。
        // The GICv2 driver arrives in M2.
        .interrupt_controller = false,
        .timer = false,
        .framebuffer = boot_info.framebuffer.address != 0,
    };
}

const char* name() { return "QEMU ARM64"; }
const char* machine() { return "QEMU virt with PL011 UART"; }
const Capabilities& capabilities() { return platform_capabilities; }

// 尚未有中斷控制器驅動程式，這些操作暫時沒有作用。
// Without an interrupt controller driver these operations do nothing yet.
void enable_irq(Irq) {}
void disable_irq(Irq) {}
void end_of_interrupt(Irq) {}
// GIC 的裝置中斷會集中送到 IRQ 例外入口，由控制器驅動程式再分辨。
// GIC device interrupts all arrive at the IRQ exception entry, where the
// controller driver works out which device raised them.
unsigned irq_vector(Irq) { return arch::arm64::current_el_spx_irq; }
// GIC 沒有 8259A 那種假中斷；讀取 IAR 拿到的 1023 代表沒有待處理中斷，
// 那要等 GICv2 驅動程式加入後才需要處理。
//
// A GIC has no equivalent of the 8259A's spurious interrupt. The 1023 that a
// read of IAR returns means no interrupt is pending, and that only becomes
// relevant once the GICv2 driver lands.
bool spurious_interrupt(Irq) { return false; }

// 尚未有計時器驅動程式。
// There is no timer driver yet.
std::uint64_t timer_ticks() { return 0; }
unsigned timer_frequency() { return 0; }

[[noreturn]] void power_off() {
    psci_call(psci_system_off);
    arch::halt();
}

[[noreturn]] void restart() {
    psci_call(psci_system_reset);
    arch::halt();
}

} // namespace shirley::platform
