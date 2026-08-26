#include "internal.hpp"

namespace shirley::platform::apple {
namespace {

// AIC（Apple Interrupt Controller）取代了 ARM 的 GIC。這裡實作 AIC 第 1 版，
// 也就是 M1（t8103）與更早的 SoC 所使用的版本；M2 之後的 AIC 2 暫不支援。
// 暫存器位移取自 Asahi Linux 專案公開的硬體說明。
//
// The AIC (Apple Interrupt Controller) replaces ARM's GIC. This implements
// AIC version 1, as used by M1 (t8103) and earlier SoCs; AIC version 2 on M2
// and later is not supported yet. The register offsets come from the Asahi
// Linux project's published hardware documentation.
constexpr std::uintptr_t register_info = 0x0004;
constexpr std::uintptr_t register_event = 0x2004;
constexpr std::uintptr_t register_mask_set = 0x4100;
constexpr std::uintptr_t register_mask_clear = 0x4180;
constexpr std::uintptr_t register_software_clear = 0x4280;
// INFO 暫存器的低 16 位元是支援的中斷數量。
// The low 16 bits of the INFO register give the supported interrupt count.
constexpr std::uint32_t info_irq_count_mask = 0xffff;

std::uintptr_t controller_base = 0;
unsigned supported_irq_count = 0;

volatile std::uint32_t* controller_register(std::uintptr_t offset) {
    return reinterpret_cast<volatile std::uint32_t*>(controller_base + offset);
}

} // namespace

void interrupt_controller_initialize(std::uintptr_t base) {
    controller_base = base;
    supported_irq_count = 0;
    if (base == 0) return;
    supported_irq_count = *controller_register(register_info) & info_irq_count_mask;
    // 開機階段先遮罩全部中斷，由驅動程式自行解除。
    // Start with every interrupt masked and let drivers unmask what they own.
    for (unsigned irq = 0; irq < supported_irq_count; irq += 32)
        *controller_register(register_mask_set + (irq / 32) * 4) = 0xffffffffu;
}

bool interrupt_controller_present() { return controller_base != 0 && supported_irq_count != 0; }

void interrupt_controller_mask(unsigned irq) {
    if (!interrupt_controller_present() || irq >= supported_irq_count) return;
    *controller_register(register_mask_set + (irq / 32) * 4) = 1u << (irq % 32);
}

void interrupt_controller_unmask(unsigned irq) {
    if (!interrupt_controller_present() || irq >= supported_irq_count) return;
    *controller_register(register_mask_clear + (irq / 32) * 4) = 1u << (irq % 32);
}

void interrupt_controller_end_of_interrupt(unsigned irq) {
    if (!interrupt_controller_present() || irq >= supported_irq_count) return;
    *controller_register(register_software_clear + (irq / 32) * 4) = 1u << (irq % 32);
    // 讀取 EVENT 暫存器會讓控制器接受下一個中斷。
    // Reading the EVENT register lets the controller deliver the next
    // interrupt.
    (void)*controller_register(register_event);
}

} // namespace shirley::platform::apple
