#pragma once

#include <cstdint>

namespace shirley::memory {

// 虛擬位址與實體位址目前都以 64 位元整數表示。
// Both virtual and physical addresses are 64-bit integers for now.
using VirtualAddress = std::uint64_t;
using PhysicalAddress = std::uint64_t;

enum class PageFlags : std::uint32_t {
    // 頁面權限旗標，可用位元 OR 組合。
    // Page permission flags, combined with bitwise OR.
    None = 0,
    Read = 1u << 0,
    Write = 1u << 1,
    Execute = 1u << 2,
    User = 1u << 3,
    Device = 1u << 4,
};

// 讓權限旗標可以像位元遮罩一樣組合與查詢。
// Let the permission flags be combined and queried like a bit mask.
constexpr PageFlags operator|(PageFlags left, PageFlags right) {
    return static_cast<PageFlags>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
constexpr PageFlags operator&(PageFlags left, PageFlags right) {
    return static_cast<PageFlags>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
constexpr PageFlags& operator|=(PageFlags& left, PageFlags right) { return left = left | right; }
constexpr bool contains(PageFlags value, PageFlags flag) {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

class AddressSpace {
public:
    // 讓多型位址空間能正確釋放資源。
    // Lets a polymorphic address space release its resources correctly.
    virtual ~AddressSpace() = default;
    // 建立虛擬頁到實體頁的映射。
    // Map a virtual page onto a physical page.
    virtual bool map(VirtualAddress, PhysicalAddress, PageFlags) = 0;
    // 移除虛擬頁映射。
    // Remove a virtual page mapping.
    virtual bool unmap(VirtualAddress) = 0;
    // 將虛擬位址轉譯成實體位址；未映射時回傳 0。
    // Translate a virtual address; returns 0 when nothing is mapped there.
    virtual PhysicalAddress translate(VirtualAddress) const = 0;
};

} // namespace shirley::memory
