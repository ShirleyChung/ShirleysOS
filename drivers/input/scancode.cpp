#include "shirley/input/scancode.hpp"

namespace shirley::input {
namespace {

// 掃描碼組 1 的按下碼對照表，索引就是掃描碼本身。0 代表該鍵沒有字元，
// 例如 Esc、修飾鍵與功能鍵。
//
// The make-code table for scancode set 1, indexed by the scancode itself. A 0
// means the key produces no character: Esc, the modifiers, and the function
// keys.
constexpr std::uint8_t table_size = 0x40;
constexpr char ascii_table[table_size] = {
    /* 0x00 */ 0, 0, '1', '2', '3', '4', '5', '6',
    /* 0x08 */ '7', '8', '9', '0', '-', '=', '\b', '\t',
    /* 0x10 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
    /* 0x18 */ 'o', 'p', '[', ']', '\n', 0, 'a', 's',
    /* 0x20 */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
    /* 0x28 */ '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
    /* 0x30 */ 'b', 'n', 'm', ',', '.', '/', 0, '*',
    /* 0x38 */ 0, ' ', 0, 0, 0, 0, 0, 0,
};

} // namespace

bool scancode_is_release(std::uint8_t code) { return (code & scancode_release_flag) != 0; }

std::uint8_t scancode_key(std::uint8_t code) {
    return static_cast<std::uint8_t>(code & ~scancode_release_flag);
}

char scancode_to_ascii(std::uint8_t code) {
    const auto key = scancode_key(code);
    return key < table_size ? ascii_table[key] : '\0';
}

char ScancodeDecoder::feed(std::uint8_t byte) {
    if (byte == scancode_extended_prefix) {
        extended_ = true;
        return '\0';
    }
    // 擴充鍵目前都沒有對應字元，因此整組直接丟棄，不能拿基本掃描碼硬解，
    // 否則方向鍵會被誤譯成一般字母。
    //
    // No extended key has a character yet, so the whole pair is dropped rather
    // than decoded as its base scancode; otherwise the arrow keys would come
    // out as ordinary letters.
    if (extended_) {
        extended_ = false;
        return '\0';
    }
    // 第一版只在按下時輸出字元，放開事件全部忽略。
    // This first version emits a character on key-down only and ignores every
    // release event.
    if (scancode_is_release(byte)) return '\0';
    return scancode_to_ascii(byte);
}

void ScancodeDecoder::reset() { extended_ = false; }

} // namespace shirley::input
