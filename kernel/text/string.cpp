#include "shirley/text.hpp"

namespace shirley::text {

std::size_t length(const char* text) {
    if (text == nullptr) return 0;
    std::size_t count = 0;
    while (text[count] != '\0') ++count;
    return count;
}

bool equals(const char* left, const char* right) {
    if (left == nullptr || right == nullptr) return left == right;
    while (*left != '\0' && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

bool equals(const char* left, const char* right, std::size_t count) {
    if (left == nullptr || right == nullptr) return left == right;
    for (std::size_t i = 0; i < count; ++i) {
        if (left[i] != right[i]) return false;
        // 兩邊同時結束時前面的比較已經確認相等，可以提早收工。
        // Both ending together means the comparison already matched, so there
        // is nothing left to check.
        if (left[i] == '\0') return true;
    }
    return true;
}

bool copy(char* destination, std::size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0) return false;
    destination[0] = '\0';
    if (source == nullptr) return true;
    const auto count = length(source);
    if (count + 1 > capacity) return false;
    for (std::size_t i = 0; i < count; ++i) destination[i] = source[i];
    destination[count] = '\0';
    return true;
}

bool append(char* destination, std::size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0) return false;
    if (source == nullptr) return true;
    const auto used = length(destination);
    const auto count = length(source);
    if (used + count + 1 > capacity) return false;
    for (std::size_t i = 0; i < count; ++i) destination[used + i] = source[i];
    destination[used + count] = '\0';
    return true;
}

bool append(char* destination, std::size_t capacity, char value) {
    const char text[2] = {value, '\0'};
    return append(destination, capacity, text);
}

} // namespace shirley::text
