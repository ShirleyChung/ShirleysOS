#pragma once

#include <cstdint>

namespace shirley::memory {

// 虛擬位址與實體位址目前都以 64 位元整數表示。
using VirtualAddress = std::uint64_t;
using PhysicalAddress = std::uint64_t;

enum class PageFlags : std::uint32_t {
    // 頁面權限旗標，可用位元 OR 組合。
    None = 0,
    Read = 1u << 0,
    Write = 1u << 1,
    Execute = 1u << 2,
    User = 1u << 3,
};

class AddressSpace {
public:
    // 讓多型位址空間能正確釋放資源。
    virtual ~AddressSpace() = default;
    // 建立虛擬頁到實體頁的映射。
    virtual bool map(VirtualAddress, PhysicalAddress, PageFlags) = 0;
    // 移除虛擬頁映射。
    virtual bool unmap(VirtualAddress) = 0;
    // 將虛擬位址轉譯成實體位址。
    virtual PhysicalAddress translate(VirtualAddress) const = 0;
};

} // namespace shirley::memory
