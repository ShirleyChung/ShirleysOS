#pragma once

#include "shirley/address_space.hpp"
#include "shirley/arch.hpp"

namespace shirley::arch::arm64 {

// AArch64 stage 1 轉換表：4 KiB 顆粒、48 位元虛擬位址、四層（L0-L3）。
// 走訪轉換表時直接以實體位址存取，因此必須在 MMU 尚未啟用，
// 或低階實體記憶體為 identity mapping 的情況下使用。
//
// AArch64 stage 1 translation tables: 4 KiB granule, 48-bit virtual
// addresses, four levels (L0-L3). Walking the tables dereferences physical
// addresses directly, so this is only valid while the MMU is off or low
// physical memory is identity-mapped.
class PageTable final : public memory::AddressSpace {
public:
    ~PageTable() override;
    // 配置 L0 轉換表；記憶體不足時回傳 false。
    // Allocate the L0 table; returns false when memory runs out.
    bool initialize();
    // 釋放整棵轉換表，但不釋放被映射的資料頁。
    // Free the whole table tree, but not the data pages it maps.
    void destroy();

    bool map(memory::VirtualAddress, memory::PhysicalAddress, memory::PageFlags) override;
    bool unmap(memory::VirtualAddress) override;
    memory::PhysicalAddress translate(memory::VirtualAddress) const override;

    // 可直接寫入 TTBR0_EL1 的 handle；尚未初始化時為 0。
    // A handle that can be written straight to TTBR0_EL1; 0 before init.
    AddressSpaceHandle handle() const { return static_cast<AddressSpaceHandle>(root_); }
    bool valid() const { return root_ != 0; }

private:
    memory::PhysicalAddress root_ = 0;
};

} // namespace shirley::arch::arm64
