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
    console::write("[IRQ] PIC remapped 0x20/0x28\n");
    // 裝置驅動程式各自向 IRQ 層註冊；此時中斷仍是關閉的，要等
    // kernel_main() 呼叫 arch::enable_interrupts() 才會真的送達。
    //
    // Each device driver registers with the IRQ layer for itself. Interrupts
    // are still disabled here and only start arriving once kernel_main() calls
    // arch::enable_interrupts().
    const bool timer = pc::pit_initialize(pc::pit_default_frequency);
    // COM1 在主控台後端初始化時就已經在輸出了，因此不論接收中斷之後成不成功，
    // 它都是一個真實存在、可以寫入的裝置，先登記起來。
    //
    // COM1 has been printing since the console backend came up, so it is a
    // real, writable device whether or not the receive interrupt succeeds
    // afterwards; it is published first.
    pc::serial_device_register();
    pc::ps2_keyboard_initialize();
    // 這台機器有兩個輸入裝置：實體鍵盤（kbd0），以及序列埠另一端的終端機
    // （uart0）。兩者各自維護自己的環狀緩衝區，再一起接上主控台，因此 shell
    // 不必知道使用者是坐在螢幕前還是接在序列線上。
    //
    // This machine has two input devices: the keyboard (kbd0) and the terminal
    // on the other end of the serial line (uart0). Each keeps its own ring
    // buffer and both attach to the console, so the shell never needs to know
    // whether the user is at the screen or on a serial cable.
    pc::serial_input_initialize();
    platform_capabilities.serial_console = true;
    platform_capabilities.interrupt_controller = true;
    platform_capabilities.timer = timer;
    platform_capabilities.framebuffer = boot_info.framebuffer.address != 0;
}

const char* name() { return "QEMU x86_64"; }
const char* machine() { return "QEMU PC with SeaBIOS firmware"; }
const Capabilities& capabilities() { return platform_capabilities; }

void enable_irq(Irq irq) { pc::pic_unmask(irq); }
void disable_irq(Irq irq) { pc::pic_mask(irq); }
void end_of_interrupt(Irq irq) { pc::pic_end_of_interrupt(irq); }
unsigned irq_vector(Irq irq) { return pc::base_vector + irq; }
bool spurious_interrupt(Irq irq) { return pc::pic_spurious(irq); }

// 這台機器的中斷控制器、計時器與序列埠都接在 I/O port 上，不是記憶體。
// port I/O 不經過分頁表，因此換到 user 位址空間之後它們照樣能存取，沒有任何
// 區段需要映射。
//
// This machine's interrupt controller, timer, and serial port are all behind
// I/O ports rather than memory. Port I/O does not go through the page tables,
// so they remain reachable after switching to a user address space and there
// is nothing to map.
std::size_t mmio_region_count() { return 0; }
MmioRegion mmio_region(std::size_t) { return {}; }

std::uint64_t timer_ticks() { return pc::pit_ticks(); }
unsigned timer_frequency() { return pc::pit_frequency(); }

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
