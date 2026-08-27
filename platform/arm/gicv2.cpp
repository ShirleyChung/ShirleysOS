#include "shirley/platform/arm/gicv2.hpp"

#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"
#include "shirley/console.hpp"
#include "shirley/irq.hpp"

namespace shirley::platform::arm {
namespace {

// 分配器暫存器位移。多數是每個中斷 1 個位元的位元圖，優先權與目標核心則是
// 每個中斷 1 個位元組。
//
// Distributor register offsets. Most are bitmaps with one bit per interrupt;
// priority and target core use one byte per interrupt instead.
constexpr std::uintptr_t distributor_control = 0x000;
constexpr std::uintptr_t distributor_type = 0x004;
constexpr std::uintptr_t distributor_set_enable = 0x100;
constexpr std::uintptr_t distributor_clear_enable = 0x180;
constexpr std::uintptr_t distributor_clear_pending = 0x280;
constexpr std::uintptr_t distributor_priority = 0x400;
constexpr std::uintptr_t distributor_target = 0x800;
constexpr std::uintptr_t distributor_configuration = 0xc00;

// CPU 介面暫存器位移。
// CPU interface register offsets.
constexpr std::uintptr_t cpu_control = 0x000;
constexpr std::uintptr_t cpu_priority_mask = 0x004;
constexpr std::uintptr_t cpu_binary_point = 0x008;
constexpr std::uintptr_t cpu_acknowledge = 0x00c;
constexpr std::uintptr_t cpu_end_of_interrupt = 0x010;

// GICD_TYPER 的低 5 位元是 ITLinesNumber，中斷線總數為 32 x (N + 1)。
// The low 5 bits of GICD_TYPER are ITLinesNumber; the line count is
// 32 x (N + 1).
constexpr std::uint32_t type_line_count_mask = 0x1f;
constexpr unsigned lines_per_field = 32;
constexpr unsigned max_interrupt_count = 1020;

// 優先權數值越小越優先。所有裝置中斷都設成同一個中等優先權，並讓遮罩值
// 比它寬鬆，中斷才通得過 CPU 介面。
//
// A lower priority value wins. Every device interrupt gets the same middle
// priority, and the mask is set looser than it so interrupts pass the CPU
// interface at all.
constexpr std::uint8_t default_priority = 0xa0;
constexpr std::uint32_t priority_mask_allow_all = 0xf0;
// 目標核心位元圖：目前只有開機核心，也就是 CPU 介面 0。
// The target core bitmap: only the boot core exists today, CPU interface 0.
constexpr std::uint8_t target_boot_core = 0x01;
constexpr std::uint32_t enable_group0 = 1u << 0;
// GICC_IAR 的低 10 位元是中斷編號，其餘位元在 SGI 時記錄來源核心。
// The low 10 bits of GICC_IAR are the interrupt number; the rest record the
// source core for an SGI.
constexpr std::uint32_t acknowledge_id_mask = 0x3ff;

std::uintptr_t distributor = 0;
std::uintptr_t cpu_interface = 0;
unsigned interrupt_count = 0;

volatile std::uint32_t* word_at(std::uintptr_t base, std::uintptr_t offset) {
    return reinterpret_cast<volatile std::uint32_t*>(base + offset);
}

volatile std::uint8_t* byte_at(std::uintptr_t base, std::uintptr_t offset) {
    return reinterpret_cast<volatile std::uint8_t*>(base + offset);
}

// 位元圖暫存器每 32 個中斷佔一個字。
// A bitmap register holds 32 interrupts per word.
volatile std::uint32_t* bitmap(std::uintptr_t offset, unsigned intid) {
    return word_at(distributor, offset + (intid / 32) * 4);
}

// 寫入週邊之後要等寫入真的生效，才能繼續往下設定。
// Writes to the peripheral have to land before configuration continues.
void barrier() { asm volatile("dsb sy" : : : "memory"); }

// GIC 的中斷會全部落在同一個 IRQ 例外入口，因此這裡負責問出實際來源。
// 一次例外可能有多個中斷待處理，所以要一直確認到 GIC 回報沒有中斷為止。
//
// Every GIC interrupt lands on the same IRQ exception entry, so this is where
// the real source is asked for. One exception can cover several pending
// interrupts, so acknowledging continues until the GIC reports none left.
void gic_exception_entry(unsigned, void*) {
    for (;;) {
        const unsigned acknowledged = *word_at(cpu_interface, cpu_acknowledge) & acknowledge_id_mask;
        // 1023 代表沒有待處理中斷，1020-1022 是其他保留值；兩者都不能送 EOI。
        // 1023 means nothing is pending and 1020-1022 are the other reserved
        // values; neither may be given an EOI.
        if (acknowledged >= gic_reserved_base) return;
        irq::dispatch(acknowledged);
    }
}

} // namespace

bool gicv2_initialize(std::uintptr_t distributor_base, std::uintptr_t cpu_interface_base) {
    distributor = 0;
    cpu_interface = 0;
    interrupt_count = 0;
    if (distributor_base == 0 || cpu_interface_base == 0) return false;

    const auto lines = *word_at(distributor_base, distributor_type) & type_line_count_mask;
    const unsigned count = (lines + 1) * lines_per_field;
    if (count <= gic_spi_base || count > max_interrupt_count) return false;

    distributor = distributor_base;
    cpu_interface = cpu_interface_base;
    interrupt_count = count;

    // 設定期間先關掉分配器，避免半設定好的中斷被送出去。
    // Turn the distributor off while it is configured, so a half-configured
    // interrupt cannot be delivered.
    *word_at(distributor, distributor_control) = 0;
    barrier();

    for (unsigned intid = 0; intid < interrupt_count; intid += 32) {
        // 開機階段遮罩全部中斷並清掉待處理狀態，由驅動程式自行解除。
        // Mask every interrupt and clear any pending state at boot, leaving
        // each driver to unmask what it owns.
        *bitmap(distributor_clear_enable, intid) = 0xffffffffu;
        *bitmap(distributor_clear_pending, intid) = 0xffffffffu;
    }
    for (unsigned intid = 0; intid < interrupt_count; ++intid) {
        byte_at(distributor, distributor_priority)[intid] = default_priority;
        // SGI 與 PPI 是每個核心私有的，目標核心欄位對它們是唯讀的。
        // SGIs and PPIs are private to a core, and the target field is
        // read-only for them.
        if (intid >= gic_spi_base) byte_at(distributor, distributor_target)[intid] = target_boot_core;
    }
    // SPI 的觸發方式每個中斷佔 2 個位元；全部設為位準觸發，這是共用裝置線
    // 唯一安全的預設值。SGI 與 PPI 的設定同樣是硬體固定的。
    //
    // An SPI's trigger mode takes two bits each; all are set to level, the
    // only safe default for a shared device line. The SGI and PPI settings are
    // likewise fixed by the hardware.
    for (unsigned intid = gic_spi_base; intid < interrupt_count; intid += 16)
        *word_at(distributor, distributor_configuration + (intid / 16) * 4) = 0;
    barrier();

    *word_at(distributor, distributor_control) = enable_group0;
    // 優先權遮罩要比中斷本身的優先權寬鬆，否則 CPU 介面會擋下每一個中斷。
    // The priority mask has to be looser than the interrupts' own priority, or
    // the CPU interface blocks every one of them.
    *word_at(cpu_interface, cpu_priority_mask) = priority_mask_allow_all;
    // 二進位點設為 0：不做優先權分組，全部依數值比較。
    // A binary point of zero means no priority grouping; values compare
    // directly.
    *word_at(cpu_interface, cpu_binary_point) = 0;
    *word_at(cpu_interface, cpu_control) = enable_group0;
    barrier();

    // GIC 把所有裝置中斷送到同一個向量，因此由控制器驅動程式親自掛上它，
    // 而不是交給 IRQ 層。EL1 預設使用 SP_ELx，但兩個 IRQ 入口都掛上，
    // 之後若改用 SP_EL0 也不會漏接。
    //
    // The GIC funnels every device interrupt into one vector, so the
    // controller driver hooks it itself rather than leaving it to the IRQ
    // layer. EL1 uses SP_ELx by default, but both IRQ entries are hooked so
    // nothing is missed if SP_EL0 is ever used instead.
    if (!arch::register_interrupt_handler(arch::arm64::current_el_spx_irq, gic_exception_entry) ||
        !arch::register_interrupt_handler(arch::arm64::current_el_sp0_irq, gic_exception_entry)) {
        distributor = 0;
        cpu_interface = 0;
        interrupt_count = 0;
        return false;
    }
    return true;
}

bool gicv2_present() { return distributor != 0 && interrupt_count != 0; }
unsigned gicv2_interrupt_count() { return interrupt_count; }

void gicv2_enable(unsigned intid) {
    if (!gicv2_present() || intid >= interrupt_count) return;
    *bitmap(distributor_set_enable, intid) = 1u << (intid % 32);
    barrier();
}

void gicv2_disable(unsigned intid) {
    if (!gicv2_present() || intid >= interrupt_count) return;
    *bitmap(distributor_clear_enable, intid) = 1u << (intid % 32);
    barrier();
}

void gicv2_end_of_interrupt(unsigned intid) {
    if (!gicv2_present() || intid >= gic_reserved_base) return;
    // SGI 的 EOI 還需要帶上來源核心編號，但目前沒有多核心也就沒有 SGI；
    // 加入處理器間中斷時這裡要改成回寫完整的 GICC_IAR 值。
    //
    // An SGI's EOI also carries the source core, but with no second core there
    // are no SGIs yet. Adding inter-processor interrupts means writing back
    // the whole GICC_IAR value here instead.
    *word_at(cpu_interface, cpu_end_of_interrupt) = intid;
}

} // namespace shirley::platform::arm
