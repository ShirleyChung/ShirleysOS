#include "internal.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"
#include "shirley/irq.hpp"

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
// 讀取 EVENT 暫存器會取走一個待處理事件：高 16 位元是事件種類，
// 低 16 位元是中斷編號。種類 0 代表已經沒有事件了。
//
// Reading the EVENT register takes one pending event: the high 16 bits are the
// event type and the low 16 bits are the interrupt number. Type 0 means there
// is nothing left.
constexpr std::uint32_t event_type_shift = 16;
constexpr std::uint32_t event_number_mask = 0xffff;
constexpr std::uint32_t event_type_none = 0;
constexpr std::uint32_t event_type_irq = 1;

std::uintptr_t controller_base = 0;
unsigned supported_irq_count = 0;

volatile std::uint32_t* controller_register(std::uintptr_t offset) {
    return reinterpret_cast<volatile std::uint32_t*>(controller_base + offset);
}

// AIC 的中斷全部落在同一個 IRQ 例外入口，因此這裡負責問出實際來源。
// 一次例外可能有多個事件待處理，所以要一直取到控制器回報沒有事件為止。
//
// Every AIC interrupt lands on the same IRQ exception entry, so this is where
// the real source is asked for. One exception can cover several pending
// events, so taking them continues until the controller reports none left.
void aic_exception_entry(unsigned, void*) {
    for (;;) {
        const std::uint32_t event = *controller_register(register_event);
        const std::uint32_t type = event >> event_type_shift;
        if (type == event_type_none) return;
        // 目前只處理裝置 IRQ。IPI 與計時器事件用的是其他種類，等多核心
        // 支援時再處理；不認得的種類必須丟掉而不是當成 IRQ 分派出去。
        //
        // Only device IRQs are handled today. IPIs and timer events use other
        // types and wait for multiprocessor support; an unrecognized type is
        // dropped rather than dispatched as though it were an IRQ.
        if (type != event_type_irq) continue;
        irq::dispatch(event & event_number_mask);
    }
}

} // namespace

bool interrupt_controller_initialize(std::uintptr_t base) {
    controller_base = base;
    supported_irq_count = 0;
    if (base == 0) return false;
    supported_irq_count = *controller_register(register_info) & info_irq_count_mask;
    if (supported_irq_count == 0) return false;
    // 開機階段先遮罩全部中斷，由驅動程式自行解除。
    // Start with every interrupt masked and let drivers unmask what they own.
    for (unsigned irq = 0; irq < supported_irq_count; irq += 32)
        *controller_register(register_mask_set + (irq / 32) * 4) = 0xffffffffu;

    // AIC 把所有裝置中斷送到同一個向量，因此由控制器驅動程式親自掛上它，
    // 而不是交給 IRQ 層。EL1 預設使用 SP_ELx，兩個 IRQ 入口都掛上比較保險。
    //
    // The AIC funnels every device interrupt into one vector, so the
    // controller driver hooks it itself rather than leaving it to the IRQ
    // layer. EL1 uses SP_ELx by default; hooking both IRQ entries is safer.
    if (!arch::register_interrupt_handler(arch::arm64::current_el_spx_irq, aic_exception_entry) ||
        !arch::register_interrupt_handler(arch::arm64::current_el_sp0_irq, aic_exception_entry)) {
        controller_base = 0;
        supported_irq_count = 0;
        return false;
    }
    return true;
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
    // 這裡只清掉中斷來源，絕對不能順手讀 EVENT 暫存器：讀取會取走「下一個」
    // 待處理事件，那個事件就再也不會被分辨出來。取事件是分辨常式的工作。
    //
    // This clears the interrupt source only and must not also read the EVENT
    // register: a read takes the *next* pending event, which would then never
    // be identified. Taking events is the demultiplexing handler's job.
    *controller_register(register_software_clear + (irq / 32) * 4) = 1u << (irq % 32);
}

} // namespace shirley::platform::apple
