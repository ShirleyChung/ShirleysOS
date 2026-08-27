#pragma once

#include "shirley/address_space.hpp"
#include "shirley/arch.hpp"

namespace shirley::arch::x86_64 {

// x86_64 的四層分頁表（PML4 / PDPT / PD / PT），只處理 4 KiB 分頁。
// 走訪分頁表時直接以實體位址存取，因此要求低階實體記憶體為 identity mapping；
// 開機載入器已將前 1 GiB 建立 identity mapping。
//
// The x86_64 four-level page tables (PML4/PDPT/PD/PT), 4 KiB pages only.
// Walking the tables dereferences physical addresses directly, so low physical
// memory must be identity-mapped; the boot loader identity-maps the first
// 1 GiB.
class PageTable final : public memory::AddressSpace {
public:
    ~PageTable() override;
    // 配置根層分頁表；記憶體不足時回傳 false。
    // Allocate the root table; returns false when memory runs out.
    bool initialize();
    // 釋放整棵分頁表，但不釋放被映射的資料頁。
    // Free the whole table tree, but not the data pages it maps.
    void destroy();

    bool map(memory::VirtualAddress, memory::PhysicalAddress, memory::PageFlags) override;
    bool unmap(memory::VirtualAddress) override;
    memory::PhysicalAddress translate(memory::VirtualAddress) const override;
    memory::PhysicalAddress root() const { return root_; }

    // 可直接寫入 CR3 的 handle；尚未初始化時為 0。
    // A handle that can be written straight to CR3; 0 before initialization.
    AddressSpaceHandle handle() const { return static_cast<AddressSpaceHandle>(root_); }
    bool valid() const { return root_ != 0; }

private:
    memory::PhysicalAddress root_ = 0;
};

} // namespace shirley::arch::x86_64
