#include "shirley/platform.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"
#include "shirley/console.hpp"
#include "shirley/platform/arm/generic_timer.hpp"
#include "shirley/platform/arm/gicv2.hpp"
#include "shirley/platform/arm/pl011.hpp"

namespace shirley::platform {
Capabilities platform_capabilities{};

namespace {

// QEMU virt 的 GICv2 固定在這兩個位址。實體機器應該從裝置樹的中斷控制器
// 節點讀出來，但目前的裝置樹解析器只認得 /memory；加入通用節點查詢之前，
// 這裡沿用 apple_silicon 對 AIC 位址的同一種做法，以已知常數運作。
//
// QEMU virt always puts its GICv2 at these two addresses. A real machine
// should read them from the device tree's interrupt controller node, but the
// device tree parser only understands /memory today; until generic node
// lookup exists this follows what apple_silicon already does for the AIC base
// and works from known constants.
constexpr std::uintptr_t virt_gic_distributor = 0x08000000;
constexpr std::uintptr_t virt_gic_cpu_interface = 0x08010000;
// 主控台輸出用的 PL011 也是主控台的輸入裝置，位址與 console.cpp 相同。
// The PL011 the console writes through is also the console's input device, at
// the same address console.cpp uses.
constexpr std::uintptr_t virt_uart = 0x09000000;

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
    // 中斷控制器要在架構層安裝例外向量表之後才掛上分辨常式。
    // The controller hooks its demultiplexing handler only after the
    // architecture layer has installed the exception vector table.
    const bool controller = arm::gicv2_initialize(virt_gic_distributor, virt_gic_cpu_interface);
    console::write(controller ? "[IRQ] GICv2 initialized\n"
                              : "[IRQ] no GICv2 found; device interrupts stay masked\n");
    // 計時器必須在控制器之後啟用：解除 PPI 30 遮罩的是分配器。
    // The timer comes up after the controller, because unmasking PPI 30 is the
    // distributor's job.
    const bool timer = controller && arm::generic_timer_initialize(arm::generic_timer_default_frequency);
    // UART 的接收中斷同樣要等分配器就緒才解得開遮罩。這台機器沒有鍵盤，
    // 序列埠就是唯一的輸入裝置，主控台 shell 完全靠它。
    //
    // The UART's receive interrupt also needs the distributor before it can be
    // unmasked. This machine has no keyboard, so the serial port is the only
    // input device and the console shell depends entirely on it.
    if (controller) arm::pl011_input_initialize(virt_uart, arm::pl011_virt_irq);
    platform_capabilities.serial_console = true;
    platform_capabilities.interrupt_controller = controller;
    platform_capabilities.timer = timer;
    platform_capabilities.framebuffer = boot_info.framebuffer.address != 0;
}

const char* name() { return "QEMU ARM64"; }
const char* machine() { return "QEMU virt with PL011 UART"; }
const Capabilities& capabilities() { return platform_capabilities; }

void enable_irq(Irq irq) { arm::gicv2_enable(irq); }
void disable_irq(Irq irq) { arm::gicv2_disable(irq); }
void end_of_interrupt(Irq irq) { arm::gicv2_end_of_interrupt(irq); }
// GIC 的裝置中斷全部集中送到同一個 IRQ 例外入口，由控制器驅動程式讀取
// GICC_IAR 分辨來源，因此這裡沒有專屬向量可以回報。
//
// Every GIC device interrupt lands on the same IRQ exception entry, and the
// controller driver reads GICC_IAR to identify the source, so there is no
// dedicated vector to report here.
unsigned irq_vector(Irq) { return demultiplexed_vector; }
// GIC 沒有 8259A 那種假中斷：讀取 GICC_IAR 拿到的 1020-1023 代表沒有可處理
// 的中斷，控制器驅動程式在分辨來源時就已經濾掉，不會走到分派這一步。
//
// A GIC has no equivalent of the 8259A's spurious interrupt. The 1020-1023 a
// read of GICC_IAR can return mean there is nothing to handle, and the
// controller driver filters those out while identifying the source, so they
// never reach dispatch.
bool spurious_interrupt(Irq) { return false; }

std::uint64_t timer_ticks() { return arm::generic_timer_ticks(); }
unsigned timer_frequency() { return arm::generic_timer_frequency(); }

namespace {

// 進入 user 空間之後，核心仍然要讀 GIC 的 CPU 介面來確認中斷、要寫 PL011
// 才能輸出，因此這三段 MMIO 必須存在於每一個位址空間裡。分配器則是解除
// 遮罩與設定路由時會用到。
//
// Once userspace is running the kernel still reads the GIC's CPU interface to
// acknowledge an interrupt and still writes the PL011 to print, so these three
// MMIO ranges have to exist in every address space. The distributor is the one
// unmasking and routing go through.
constexpr MmioRegion regions[] = {
    {virt_uart, 0x1000},
    {virt_gic_distributor, 0x10000},
    {virt_gic_cpu_interface, 0x2000},
};

} // namespace

std::size_t mmio_region_count() { return sizeof(regions) / sizeof(regions[0]); }

MmioRegion mmio_region(std::size_t index) {
    return index < mmio_region_count() ? regions[index] : MmioRegion{};
}

[[noreturn]] void power_off() {
    psci_call(psci_system_off);
    arch::halt();
}

[[noreturn]] void restart() {
    psci_call(psci_system_reset);
    arch::halt();
}

} // namespace shirley::platform
