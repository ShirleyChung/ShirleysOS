#include "shirley/platform.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"
#include "shirley/console.hpp"
#include "shirley/platform/arm/generic_timer.hpp"
#include "shirley/platform/arm/gicv2.hpp"

namespace shirley::platform {
namespace {

Capabilities platform_capabilities{};

// 同一台 QEMU virt，只是換成 UEFI 韌體，因此 GICv2 位址與 BIOS 路徑相同。
// The same QEMU virt machine with UEFI firmware instead, so the GICv2 sits at
// the same addresses as on the non-UEFI path.
constexpr std::uintptr_t virt_gic_distributor = 0x08000000;
constexpr std::uintptr_t virt_gic_cpu_interface = 0x08010000;

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
    // UEFI 韌體離開時會留下自己設定的 GIC，必須重新設定成核心要的狀態。
    // UEFI firmware leaves its own GIC configuration behind, so it is
    // reprogrammed into the state the kernel wants.
    const bool controller = arm::gicv2_initialize(virt_gic_distributor, virt_gic_cpu_interface);
    console::write(controller ? "[IRQ] GICv2 initialized\n"
                              : "[IRQ] no GICv2 found; device interrupts stay masked\n");
    const bool timer = controller && arm::generic_timer_initialize(arm::generic_timer_default_frequency);
    platform_capabilities = {
        .serial_console = true,
        .interrupt_controller = controller,
        .timer = timer,
        // UEFI 的 GOP framebuffer 由開機載入器記錄在開機資訊中。
        // The UEFI GOP framebuffer is recorded in the boot information by the
        // boot loader.
        .framebuffer = boot_info.framebuffer.address != 0,
    };
}

const char* name() { return "QEMU ARM64 (UEFI)"; }
const char* machine() { return "QEMU virt with EDK2 UEFI firmware"; }
const Capabilities& capabilities() { return platform_capabilities; }

void enable_irq(Irq irq) { arm::gicv2_enable(irq); }
void disable_irq(Irq irq) { arm::gicv2_disable(irq); }
void end_of_interrupt(Irq irq) { arm::gicv2_end_of_interrupt(irq); }
// GIC 的裝置中斷全部集中送到同一個 IRQ 例外入口，由控制器驅動程式讀取
// GICC_IAR 分辨來源。
//
// Every GIC device interrupt lands on the same IRQ exception entry, and the
// controller driver reads GICC_IAR to identify the source.
unsigned irq_vector(Irq) { return demultiplexed_vector; }
// GIC 沒有 8259A 那種假中斷；保留編號在分辨來源時就被濾掉了。
// A GIC has no equivalent of the 8259A's spurious interrupt; the reserved
// numbers are filtered out while the source is identified.
bool spurious_interrupt(Irq) { return false; }

std::uint64_t timer_ticks() { return arm::generic_timer_ticks(); }
unsigned timer_frequency() { return arm::generic_timer_frequency(); }

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
