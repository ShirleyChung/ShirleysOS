#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley::format {

// 將數值轉成十進位字串並以 null 結尾，回傳寫入的字元數（不含結尾）。
// 緩衝區不足時輸出空字串並回傳 0，避免截斷的數字被誤讀成真實數值。
// Write value as a null-terminated decimal string and return the character
// count, excluding the terminator. When the buffer is too small the result is
// an empty string and 0, so a truncated number can never be mistaken for a
// real value.
std::size_t to_decimal(char* buffer, std::size_t capacity, std::uint64_t value);
// 將數值轉成小寫十六進位字串，必要時前補零到 min_digits 位。
// Write value as a lowercase hexadecimal string, zero-padded to min_digits.
std::size_t to_hex(char* buffer, std::size_t capacity, std::uint64_t value, std::size_t min_digits = 1);

} // namespace shirley::format
