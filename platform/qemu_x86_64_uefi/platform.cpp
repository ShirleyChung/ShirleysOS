#include "shirley/platform.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/x86_64/port_io.hpp"
#include "shirley/platform/pc/pic.hpp"

namespace shirley::platform {
namespace {

using arch::x86_64::outb;
using arch::x86_64::outw;

Capabilities platform_capabilities{};

// QEMU 的 ACPI 電源管理暫存器；OVMF 預設使用的 i440FX 與 Q35 各有一個 port。
// QEMU's ACPI power management register; the i440FX and Q35 machines OVMF runs
// on each use a different port.
constexpr std::uint16_t acpi_power_i440fx = 0x604;
constexpr std::uint16_t acpi_power_q35 = 0xb004;
constexpr std::uint16_t acpi_sleep_enable = 0x2000;
constexpr std::uint16_t keyboard_command = 0x64;
constexpr std::uint8_t keyboard_reset = 0xfe;

} // namespace

void initialize(const BootInfo& boot_info) {
    // UEFI 韌體離開時會留下自己設定的 8259，必須重新對應成核心的向量配置。
    // UEFI firmware leaves its own 8259 configuration behind, so the vectors
    // have to be remapped onto the kernel's layout.
    pc::pic_initialize();
    platform_capabilities = {
        .serial_console = true,
        .interrupt_controller = true,
        // 計時器驅動程式在 M2 才會加入。
        // The timer driver arrives in M2.
        .timer = false,
        // UEFI 的 GOP framebuffer 由開機載入器記錄在開機資訊中。
        // The UEFI GOP framebuffer is recorded in the boot information by the
        // boot loader.
        .framebuffer = boot_info.framebuffer.address != 0,
    };
}

const char* name() { return "QEMU x86_64 (UEFI)"; }
const char* machine() { return "QEMU PC with OVMF UEFI firmware"; }
const Capabilities& capabilities() { return platform_capabilities; }

void enable_irq(Irq irq) { pc::pic_unmask(irq); }
void disable_irq(Irq irq) { pc::pic_mask(irq); }
void end_of_interrupt(Irq irq) { pc::pic_end_of_interrupt(irq); }
unsigned irq_vector(Irq irq) { return pc::base_vector + irq; }

// 核心已經呼叫過 ExitBootServices，因此不能再使用 UEFI runtime services 的
// ResetSystem；這裡直接操作硬體。
// The kernel has already gone through ExitBootServices, so the UEFI runtime
// ResetSystem service is not used; the hardware is driven directly instead.
[[noreturn]] void power_off() {
    outw(acpi_power_i440fx, acpi_sleep_enable);
    outw(acpi_power_q35, acpi_sleep_enable);
    arch::halt();
}

[[noreturn]] void restart() {
    outb(keyboard_command, keyboard_reset);
    arch::halt();
}

} // namespace shirley::platform
