#pragma once

#include <cstddef>

namespace shirley::console {

// 初始化目前平台的主控台輸出裝置。
// Initialize the current platform's console output device.
void initialize();
// 輸出以 null 結尾的字串。
// Write a null-terminated string.
void write(const char* text);
// 輸出指定長度的位元組序列。
// Write a byte sequence of the given length.
void write(const char* text, std::size_t length);

} // namespace shirley::console
