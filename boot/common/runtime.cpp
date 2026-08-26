#include <cstddef>

// 開機載入器和核心一樣是 freestanding 二進位檔，編譯器同樣會為結構複製與清零
// 產生 memcpy/memset 呼叫。這裡不能沿用 kernel/runtime/，因為那份實作在錯誤路徑上
// 會呼叫 arch::halt()，而載入器還在韌體環境中執行，沒有架構層可用。
//
// A boot loader is a freestanding binary just like the kernel, so the compiler
// emits the same memcpy/memset calls for structure copies and zeroing. It
// cannot share kernel/runtime/, because that implementation calls arch::halt()
// on its failure paths and the loader is still inside the firmware environment
// with no architecture layer available.
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

} // extern "C"
