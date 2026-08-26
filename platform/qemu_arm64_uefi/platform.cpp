#include "shirley/platform.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"

namespace shirley::platform {
namespace {

Capabilities platform_capabilities{};

// QEMU virt 的 PSCI 介面預設使用 HVC 呼叫慣例，UEFI 開機路徑同樣適用。
// The PSCI interface on QEMU virt uses the HVC calling convention by default,
// and that stays true on the UEFI boot path.
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
        // UEFI 的 GOP framebuffer 由開機載入器記錄在開機資訊中。
        // The UEFI GOP framebuffer is recorded in the boot information by the
        // boot loader.
        .framebuffer = boot_info.framebuffer.address != 0,
    };
}

const char* name() { return "QEMU ARM64 (UEFI)"; }
const char* machine() { return "QEMU virt with EDK2 UEFI firmware"; }
const Capabilities& capabilities() { return platform_capabilities; }

// 尚未有中斷控制器驅動程式，這些操作暫時沒有作用。
// Without an interrupt controller driver these operations do nothing yet.
void enable_irq(Irq) {}
void disable_irq(Irq) {}
void end_of_interrupt(Irq) {}
unsigned irq_vector(Irq) { return arch::arm64::current_el_spx_irq; }

// 核心已經呼叫過 ExitBootServices，因此改用 PSCI 而不是 UEFI runtime services。
// The kernel has already gone through ExitBootServices, so PSCI is used rather
// than the UEFI runtime services.
[[noreturn]] void power_off() {
    psci_call(psci_system_off);
    arch::halt();
}

[[noreturn]] void restart() {
    psci_call(psci_system_reset);
    arch::halt();
}

} // namespace shirley::platform
