#include "shirley/platform.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/x86_64/port_io.hpp"
#include "shirley/platform/pc/pic.hpp"

namespace shirley::platform {
namespace {

using arch::x86_64::outb;
using arch::x86_64::outw;

Capabilities platform_capabilities{};

// QEMU 的 ACPI 電源管理暫存器；i440FX 與 Q35 使用不同的 I/O port。
// QEMU's ACPI power management register; i440FX and Q35 use different ports.
constexpr std::uint16_t acpi_power_i440fx = 0x604;
constexpr std::uint16_t acpi_power_q35 = 0xb004;
constexpr std::uint16_t acpi_sleep_enable = 0x2000;
// 傳統鍵盤控制器的 CPU reset 指令。
// The legacy keyboard controller's CPU reset command.
constexpr std::uint16_t keyboard_command = 0x64;
constexpr std::uint8_t keyboard_reset = 0xfe;

} // namespace

void initialize(const BootInfo& boot_info) {
    // 中斷控制器要在架構層安裝 IDT 之後才重新對應向量。
    // Remap the controller's vectors only after the architecture layer has
    // installed the IDT.
    pc::pic_initialize();
    platform_capabilities = {
        .serial_console = true,
        .interrupt_controller = true,
        // PIT 與 HPET 驅動程式在 M2 才會加入。
        // PIT and HPET drivers arrive in M2.
        .timer = false,
        .framebuffer = boot_info.framebuffer.address != 0,
    };
}

const char* name() { return "QEMU x86_64"; }
const char* machine() { return "QEMU PC with SeaBIOS firmware"; }
const Capabilities& capabilities() { return platform_capabilities; }

void enable_irq(Irq irq) { pc::pic_unmask(irq); }
void disable_irq(Irq irq) { pc::pic_mask(irq); }
void end_of_interrupt(Irq irq) { pc::pic_end_of_interrupt(irq); }
unsigned irq_vector(Irq irq) { return pc::base_vector + irq; }

[[noreturn]] void power_off() {
    // 兩種機型都試一次，失敗時退回停機。
    // Try both machine types, then fall back to halting.
    outw(acpi_power_i440fx, acpi_sleep_enable);
    outw(acpi_power_q35, acpi_sleep_enable);
    arch::halt();
}

[[noreturn]] void restart() {
    outb(keyboard_command, keyboard_reset);
    arch::halt();
}

} // namespace shirley::platform
