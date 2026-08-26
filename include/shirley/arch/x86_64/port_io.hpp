#pragma once

#include <cstdint>

// x86_64 的 I/O port 存取屬於 ISA 功能，因此由架構層提供。
// 只有 arch/x86_64 與 x86_64 平台程式碼可以引用；通用核心不得使用。
//
// Port I/O is an x86_64 ISA feature, so the architecture layer provides it.
// Only arch/x86_64 and x86_64 platform code may include this header; generic
// kernel code must not.
namespace shirley::arch::x86_64 {

inline void outb(std::uint16_t port, std::uint8_t value) {
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}
inline std::uint8_t inb(std::uint16_t port) {
    std::uint8_t value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
inline void outw(std::uint16_t port, std::uint16_t value) {
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}
inline std::uint16_t inw(std::uint16_t port) {
    std::uint16_t value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}
// 對慢速的傳統裝置寫入後短暫等待。
// Give a slow legacy device a moment to settle after a write.
inline void io_wait() { outb(0x80, 0); }

} // namespace shirley::arch::x86_64
