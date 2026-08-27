#pragma once

#include <cstdint>

// PS/2 掃描碼組 1 的解碼。這一層完全不碰硬體：它只把位元組序列翻譯成字元，
// 因此可以編進主機建置並由測試涵蓋，鍵盤驅動程式則只負責讀取連接埠。
//
// PS/2 scancode set 1 decoding. This layer touches no hardware at all: it only
// translates a byte stream into characters, so it compiles into the host build
// and is covered by tests, leaving the keyboard driver responsible for nothing
// but reading the port.
namespace shirley::input {

// 放開按鍵的掃描碼是按下時的掃描碼加上位元 7。
// The scancode for releasing a key is its make code with bit 7 set.
constexpr std::uint8_t scancode_release_flag = 0x80;
// 擴充鍵（方向鍵、右側 Ctrl/Alt 等）在掃描碼前多送一個前綴位元組。
// An extended key — the arrows, the right-hand Ctrl and Alt — prefixes its
// scancode with an extra byte.
constexpr std::uint8_t scancode_extended_prefix = 0xe0;

bool scancode_is_release(std::uint8_t code);
// 去掉放開旗標，取得按鍵本身的掃描碼。
// Strip the release flag to get the key's own scancode.
std::uint8_t scancode_key(std::uint8_t code);
// 將按下的掃描碼翻譯成 ASCII；沒有對應字元時回傳 0。
// 第一版只支援未加修飾鍵的小寫字母、數字、Enter、Backspace、Tab、空白與
// 基本標點，Shift、Ctrl、Alt 一律忽略。
//
// Translate a make code into ASCII, returning 0 when the key has no character.
// This first version covers unmodified lowercase letters, digits, Enter,
// Backspace, Tab, space, and basic punctuation; Shift, Ctrl, and Alt are
// ignored.
char scancode_to_ascii(std::uint8_t code);

// 把一串掃描碼位元組轉成字元。解碼器需要狀態，因為擴充鍵的前綴位元組會
// 影響下一個位元組的意義。
//
// Turns a run of scancode bytes into characters. The decoder needs state
// because an extended key's prefix byte changes what the next byte means.
class ScancodeDecoder {
public:
    // 餵入一個掃描碼位元組；沒有字元可輸出時回傳 0。
    // Feed one scancode byte; returns 0 when there is no character to emit.
    char feed(std::uint8_t byte);
    void reset();

private:
    bool extended_ = false;
};

} // namespace shirley::input
