#include "shirley/platform.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/x86_64/port_io.hpp"
#include "shirley/console.hpp"
#include "shirley/platform/pc/pic.hpp"
#include "shirley/platform/pc/pit.hpp"
#include "shirley/platform/pc/ps2_keyboard.hpp"
#include "shirley/platform/pc/serial.hpp"

namespace shirley::platform {
Capabilities platform_capabilities{};
namespace {

using arch::x86_64::outb;
using arch::x86_64::outw;

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
    console::write("[IRQ] PIC remapped 0x20/0x28\n");
    // 韌體也在使用 PS/2 控制器與計時器，因此兩個驅動程式都會把硬體重新設定
    // 成核心要的狀態，而不是沿用 ExitBootServices 之後殘留的設定。
    //
    // The firmware was using the PS/2 controller and the timer too, so both
    // drivers reprogram the hardware into the state the kernel wants instead
    // of inheriting whatever survived ExitBootServices.
    const bool timer = pc::pit_initialize(pc::pit_default_frequency);
    pc::ps2_keyboard_initialize();
    // 序列埠終端機和鍵盤一樣是主控台輸入來源；兩者共用同一個佇列。
    // A serial terminal is as much a console input source as the keyboard is,
    // and both share the same queue.
    pc::serial_input_initialize();
    platform_capabilities.serial_console = true;
    platform_capabilities.interrupt_controller = true;
    platform_capabilities.timer = timer;
        // UEFI 的 GOP framebuffer 由開機載入器記錄在開機資訊中。
        // The UEFI GOP framebuffer is recorded in the boot information by the
        // boot loader.
    platform_capabilities.framebuffer = boot_info.framebuffer.address != 0;
}

const char* name() { return "QEMU x86_64 (UEFI)"; }
const char* machine() { return "QEMU PC with OVMF UEFI firmware"; }
const Capabilities& capabilities() { return platform_capabilities; }

void enable_irq(Irq irq) { pc::pic_unmask(irq); }
void disable_irq(Irq irq) { pc::pic_mask(irq); }
void end_of_interrupt(Irq irq) { pc::pic_end_of_interrupt(irq); }
unsigned irq_vector(Irq irq) { return pc::base_vector + irq; }
bool spurious_interrupt(Irq irq) { return pc::pic_spurious(irq); }

// 和 BIOS 路徑一樣，這裡的裝置都在 I/O port 上；port I/O 不經過分頁表，
// 因此沒有任何區段需要在 user 位址空間裡映射。
//
// As on the non-UEFI path, these devices live behind I/O ports, and port I/O
// does not go through the page tables, so nothing has to be mapped into a user
// address space.
std::size_t mmio_region_count() { return 0; }
MmioRegion mmio_region(std::size_t) { return {}; }

std::uint64_t timer_ticks() { return pc::pit_ticks(); }
unsigned timer_frequency() { return pc::pit_frequency(); }

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
