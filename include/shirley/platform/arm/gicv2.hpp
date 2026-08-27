#pragma once

#include <cstdint>

// ARM 的 Generic Interrupt Controller 第 2 版，QEMU virt 與多數 ARM64 開發板
// 共用。GICv2 由兩塊暫存器組成：分配器決定哪些中斷送給哪個核心，
// CPU 介面則是核心用來確認與結束中斷的窗口。
//
// ARM's Generic Interrupt Controller version 2, shared by QEMU virt and most
// ARM64 development boards. A GICv2 is two register blocks: the distributor
// decides which interrupts go to which core, and the CPU interface is the
// window a core uses to acknowledge and finish an interrupt.
namespace shirley::platform::arm {

// GICv2 的中斷編號分成三段。SGI 由軟體發給其他核心，PPI 是每個核心私有的
// 週邊（架構計時器就在這裡），SPI 才是整台機器共用的裝置中斷。
//
// A GICv2 interrupt number falls in one of three ranges. An SGI is software
// generated for another core, a PPI is a peripheral private to one core (the
// architected timer lives here), and an SPI is a device interrupt shared by
// the whole machine.
constexpr unsigned gic_sgi_base = 0;
constexpr unsigned gic_ppi_base = 16;
constexpr unsigned gic_spi_base = 32;
// 1020-1023 是保留編號；1023 代表「沒有待處理中斷」，是確認中斷時的結束條件。
// 1020-1023 are reserved; 1023 means "no interrupt pending" and is the
// terminating condition when acknowledging interrupts.
constexpr unsigned gic_reserved_base = 1020;

// 初始化分配器與 CPU 介面，並把分辨來源的處理常式掛上 IRQ 例外入口。
// 位址為 0 或硬體沒有回報任何中斷線時回傳 false。
//
// Initialize the distributor and the CPU interface, and hook the
// source-identifying handler onto the IRQ exception entry. Returns false when
// an address is zero or the hardware reports no interrupt lines at all.
bool gicv2_initialize(std::uintptr_t distributor_base, std::uintptr_t cpu_interface_base);
bool gicv2_present();
// 硬體回報支援的中斷編號數量。
// How many interrupt numbers the hardware reports it supports.
unsigned gicv2_interrupt_count();

void gicv2_enable(unsigned intid);
void gicv2_disable(unsigned intid);
void gicv2_end_of_interrupt(unsigned intid);

} // namespace shirley::platform::arm
