#pragma once

#include <cstddef>

namespace shirley::console {

// 初始化目前平台的主控台輸出裝置。
void initialize();
// 輸出以 null 結尾的字串。
void write(const char* text);
// 輸出指定長度的位元組序列。
void write(const char* text, std::size_t length);

} // namespace shirley::console
