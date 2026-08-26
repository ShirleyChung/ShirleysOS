#include "shirley/format.hpp"

namespace shirley::format {
namespace {

// 將暫存的反向數字複製到輸出緩衝區並補上結尾。
// Copy the reversed scratch digits into the caller's buffer and terminate it.
std::size_t emit(char* buffer, std::size_t capacity, const char* digits, std::size_t count) {
    if (buffer == nullptr || capacity == 0) return 0;
    if (count + 1 > capacity) { buffer[0] = '\0'; return 0; }
    for (std::size_t i = 0; i < count; ++i) buffer[i] = digits[count - 1 - i];
    buffer[count] = '\0';
    return count;
}

} // namespace

std::size_t to_decimal(char* buffer, std::size_t capacity, std::uint64_t value) {
    // 64 位元十進位最多 20 位數。
    // A 64-bit decimal value needs at most 20 digits.
    char digits[20];
    std::size_t count = 0;
    do { digits[count++] = static_cast<char>('0' + value % 10); value /= 10; } while (value != 0);
    return emit(buffer, capacity, digits, count);
}

std::size_t to_hex(char* buffer, std::size_t capacity, std::uint64_t value, std::size_t min_digits) {
    // 64 位元十六進位最多 16 位數；min_digits 超過時以 16 為上限。
    // A 64-bit hexadecimal value needs at most 16 digits, which also caps
    // min_digits.
    char digits[16];
    if (min_digits > sizeof(digits)) min_digits = sizeof(digits);
    std::size_t count = 0;
    do { digits[count++] = "0123456789abcdef"[value & 0xf]; value >>= 4; } while (value != 0);
    while (count < min_digits) digits[count++] = '0';
    return emit(buffer, capacity, digits, count);
}

} // namespace shirley::format
