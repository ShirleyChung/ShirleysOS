#include "shirley/console.hpp"

#include <cstdio>

namespace shirley::console {
// 主機測試版本使用標準輸出模擬核心主控台。
// The host test build emulates the kernel console with standard output.
void initialize() {}
// 輸出 null 結尾字串。
// Write a null-terminated string.
void write(const char* text) { if (text) std::fputs(text, stdout); }
// 輸出指定長度，避免輸入未以 null 結尾時越界讀取。
// Write an explicit length, so input that is not null-terminated cannot be
// read past its end.
void write(const char* text, std::size_t length) { if (text) std::fwrite(text, 1, length, stdout); }
} // namespace shirley::console
