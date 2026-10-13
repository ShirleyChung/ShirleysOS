#include "shirley/text.hpp"

namespace shirley::text {
namespace {

bool high_surrogate(char32_t unit) { return unit >= 0xD800 && unit <= 0xDBFF; }
bool low_surrogate(char32_t unit) { return unit >= 0xDC00 && unit <= 0xDFFF; }

} // namespace

bool utf16_to_utf8(const char16_t* source, std::size_t count, char* destination,
                   std::size_t capacity) {
    if (destination == nullptr || capacity == 0) return false;
    destination[0] = '\0';
    if (count != 0 && source == nullptr) return false;
    std::size_t used = 0;
    const auto put = [&](std::size_t bytes, char32_t point) {
        // 留一個位元組給結尾的 null。
        // Keep one byte back for the terminating null.
        if (bytes >= capacity - used) return false;
        static constexpr unsigned char lead[] = {0, 0, 0xC0, 0xE0, 0xF0};
        for (std::size_t i = bytes; i > 1; --i) {
            destination[used + i - 1] = static_cast<char>(0x80 | (point & 0x3F));
            point >>= 6;
        }
        destination[used] = static_cast<char>(lead[bytes] | point);
        used += bytes;
        return true;
    };
    for (std::size_t i = 0; i < count; ++i) {
        char32_t point = source[i];
        // 名稱中間的 null 會讓字串提前結束，等於換了一個名字。
        // A null inside a name would end the string early, which names
        // something else.
        if (point == 0) {
            destination[0] = '\0';
            return false;
        }
        if (high_surrogate(point) && i + 1 < count && low_surrogate(source[i + 1])) {
            point = 0x10000 + ((point - 0xD800) << 10) + (source[i + 1] - 0xDC00);
            ++i;
        }
        const std::size_t bytes = point < 0x80 ? 1 : point < 0x800 ? 2 : point < 0x10000 ? 3 : 4;
        if (!put(bytes, point)) {
            destination[0] = '\0';
            return false;
        }
    }
    destination[used] = '\0';
    return true;
}

bool utf8_to_utf16(const char* source, char16_t* destination, std::size_t capacity,
                   std::size_t& count) {
    count = 0;
    if (source == nullptr || (capacity != 0 && destination == nullptr)) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(source);
    // 上一個單元是否是單獨編碼的高代理。WTF-8 不允許把一對代理拆成兩個三位元
    // 組序列，否則同一個名字會有兩種寫法。
    //
    // Whether the previous unit was a separately encoded high surrogate. WTF-8
    // forbids splitting a surrogate pair into two three-byte sequences, or one
    // name would have two spellings.
    bool after_lone_high = false;
    while (*bytes != 0) {
        const unsigned char lead = *bytes;
        std::size_t length = 0;
        char32_t point = 0;
        char32_t minimum = 0;
        if (lead < 0x80) {
            length = 1, point = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            length = 2, point = lead & 0x1F, minimum = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            length = 3, point = lead & 0x0F, minimum = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            length = 4, point = lead & 0x07, minimum = 0x10000;
        } else {
            return false;
        }
        for (std::size_t i = 1; i < length; ++i) {
            if ((bytes[i] & 0xC0) != 0x80) return false;
            point = (point << 6) | (bytes[i] & 0x3F);
        }
        // 過長的編碼與超出 Unicode 範圍的值都不合法。
        // Overlong encodings and values beyond Unicode are not valid.
        if (point < minimum || point > 0x10FFFF) return false;
        if (after_lone_high && low_surrogate(point)) return false;
        after_lone_high = high_surrogate(point);
        bytes += length;

        if (point >= 0x10000) {
            if (capacity - count < 2) return false;
            point -= 0x10000;
            destination[count++] = static_cast<char16_t>(0xD800 + (point >> 10));
            destination[count++] = static_cast<char16_t>(0xDC00 + (point & 0x3FF));
        } else {
            if (capacity - count < 1) return false;
            destination[count++] = static_cast<char16_t>(point);
        }
    }
    return true;
}

} // namespace shirley::text
