#include "shirley/arch.hpp"

#include <cstddef>

// 即使指定 -ffreestanding，編譯器仍可能為結構複製與清零產生 memcpy/memset 呼叫，
// 而多型類別的解構子會需要 operator delete。裸機核心沒有 libc 可連結，
// 因此在這裡提供這些執行期支援常式。
//
// Even under -ffreestanding the compiler can emit memcpy/memset calls for
// structure copies and zeroing, and a polymorphic destructor pulls in
// operator delete. A freestanding kernel has no libc to link against, so these
// runtime support routines live here.
extern "C" {

void* memset(void* destination, int value, std::size_t count) {
    auto* bytes = static_cast<unsigned char*>(destination);
    const auto fill = static_cast<unsigned char>(value);
    for (std::size_t i = 0; i < count; ++i) bytes[i] = fill;
    return destination;
}

void* memcpy(void* destination, const void* source, std::size_t count) {
    auto* to = static_cast<unsigned char*>(destination);
    const auto* from = static_cast<const unsigned char*>(source);
    for (std::size_t i = 0; i < count; ++i) to[i] = from[i];
    return destination;
}

void* memmove(void* destination, const void* source, std::size_t count) {
    auto* to = static_cast<unsigned char*>(destination);
    const auto* from = static_cast<const unsigned char*>(source);
    if (to == from || count == 0) return destination;
    // 目的地在來源之後時必須反向複製，避免覆蓋尚未讀取的位元組。
    // When the destination overlaps after the source, copy backwards so no
    // byte is overwritten before it has been read.
    if (to < from) {
        for (std::size_t i = 0; i < count; ++i) to[i] = from[i];
    } else {
        for (std::size_t i = count; i > 0; --i) to[i - 1] = from[i - 1];
    }
    return destination;
}

int memcmp(const void* left, const void* right, std::size_t count) {
    const auto* a = static_cast<const unsigned char*>(left);
    const auto* b = static_cast<const unsigned char*>(right);
    for (std::size_t i = 0; i < count; ++i)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

// 呼叫到純虛擬函式代表物件已損毀，直接停機比繼續執行安全。
// Reaching a pure virtual call means the object is already corrupt, so halting
// is safer than carrying on.
void __cxa_pure_virtual() { shirley::arch::halt(); }

} // extern "C"

// 核心尚未提供堆積，任何 delete 都是程式錯誤而不是正常的釋放路徑。
// The kernel has no heap yet, so any delete is a bug rather than a normal
// release path.
void operator delete(void*) noexcept { shirley::arch::halt(); }
void operator delete(void*, std::size_t) noexcept { shirley::arch::halt(); }
void operator delete[](void*) noexcept { shirley::arch::halt(); }
void operator delete[](void*, std::size_t) noexcept { shirley::arch::halt(); }
