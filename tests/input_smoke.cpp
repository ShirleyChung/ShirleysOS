#include "shirley/input/scancode.hpp"
#include "shirley/input_queue.hpp"

#include <cassert>
#include <cstring>

namespace {

using shirley::input::ScancodeDecoder;
using shirley::input::scancode_extended_prefix;
using shirley::input::scancode_is_release;
using shirley::input::scancode_key;
using shirley::input::scancode_release_flag;
using shirley::input::scancode_to_ascii;

// 掃描碼組 1 中，按下與放開只差在位元 7。
// In scancode set 1 a press and a release differ only in bit 7.
void test_release_flag() {
    assert(!scancode_is_release(0x1e));
    assert(scancode_is_release(0x1e | scancode_release_flag));
    assert(scancode_key(0x1e | scancode_release_flag) == 0x1e);
    assert(scancode_key(0x1e) == 0x1e);
}

// 題目要求的最小按鍵集合：字母、數字、Enter、Backspace、空白。
// The minimum key set the task calls for: letters, digits, Enter, Backspace,
// and space.
void test_ascii_table() {
    assert(scancode_to_ascii(0x1e) == 'a');
    assert(scancode_to_ascii(0x30) == 'b');
    assert(scancode_to_ascii(0x2e) == 'c');
    assert(scancode_to_ascii(0x10) == 'q');
    assert(scancode_to_ascii(0x19) == 'p');
    assert(scancode_to_ascii(0x02) == '1');
    assert(scancode_to_ascii(0x0b) == '0');
    assert(scancode_to_ascii(0x1c) == '\n');
    assert(scancode_to_ascii(0x0e) == '\b');
    assert(scancode_to_ascii(0x39) == ' ');
    // 修飾鍵、Esc 與功能鍵沒有字元。
    // The modifiers, Esc, and the function keys have no character.
    assert(scancode_to_ascii(0x01) == '\0');
    assert(scancode_to_ascii(0x2a) == '\0');
    assert(scancode_to_ascii(0x1d) == '\0');
    assert(scancode_to_ascii(0x3b) == '\0');
    // 超出表格的掃描碼不得越界讀取。
    // A scancode past the end of the table must not read out of bounds.
    assert(scancode_to_ascii(0x7f) == '\0');
}

// 解碼器只在按下時輸出字元，放開事件一律忽略。
// The decoder emits a character on key-down only and ignores every release.
void test_decoder_ignores_releases() {
    ScancodeDecoder decoder;
    assert(decoder.feed(0x1e) == 'a');
    assert(decoder.feed(0x1e | scancode_release_flag) == '\0');
    assert(decoder.feed(0x39) == ' ');
    assert(decoder.feed(0x39 | scancode_release_flag) == '\0');
}

// 擴充鍵的兩個位元組要整組丟掉，不能把第二個位元組當成一般掃描碼解讀。
// Both bytes of an extended key are dropped; the second must not be read as an
// ordinary scancode.
void test_decoder_extended_keys() {
    ScancodeDecoder decoder;
    assert(decoder.feed(scancode_extended_prefix) == '\0');
    // 0x4b 是方向鍵左，其基本掃描碼不在表中；0x1c 是小鍵盤 Enter，
    // 基本掃描碼是 Enter，如果沒有處理前綴就會被誤譯成換行。
    //
    // 0x4b is the left arrow, whose base scancode is not in the table; 0x1c is
    // the keypad Enter, whose base scancode is Enter and would be mistranslated
    // into a newline if the prefix were ignored.
    assert(decoder.feed(0x4b) == '\0');
    assert(decoder.feed(scancode_extended_prefix) == '\0');
    assert(decoder.feed(0x1c) == '\0');
    // 前綴狀態必須已經清掉，下一個一般按鍵才能正常解出。
    // The prefix state must be cleared so the next ordinary key decodes again.
    assert(decoder.feed(0x1e) == 'a');

    decoder.feed(scancode_extended_prefix);
    decoder.reset();
    assert(decoder.feed(0x1c) == '\n');
}

// 打一個字所產生的完整位元組序列必須剛好解出那個字。
// The full byte sequence of typing a word must decode to exactly that word.
void test_decoder_typed_word() {
    ScancodeDecoder decoder;
    const unsigned char typed[] = {
        0x1e, 0x9e,       // a 按下、放開 / a down, up
        0x30, 0xb0,       // b
        0x2e, 0xae,       // c
        0x39, 0xb9,       // space
        0x02, 0x82,       // 1
        0x0e, 0x8e,       // backspace
        0x1c, 0x9c,       // enter
    };
    char decoded[16] = {};
    std::size_t length = 0;
    for (unsigned char byte : typed) {
        const char value = decoder.feed(byte);
        if (value != '\0') decoded[length++] = value;
    }
    assert(length == 7);
    assert(std::strcmp(decoded, "abc 1\b\n") == 0);
}

// 佇列是先進先出，而且讀取後空間會被回收。
// The queue is first in, first out, and space is reclaimed after a read.
void test_queue_round_trip() {
    shirley::io::InputQueue queue;
    assert(queue.empty());
    assert(queue.available() == 0);
    assert(queue.push('h'));
    assert(queue.push('i'));
    assert(queue.available() == 2);
    assert(!queue.empty());

    char buffer[8] = {};
    auto result = queue.read(buffer, sizeof(buffer));
    assert(result);
    assert(result.transferred == 2);
    assert(buffer[0] == 'h' && buffer[1] == 'i');
    assert(queue.empty());

    // 空佇列的讀取不是錯誤，只是沒有位元組。
    // Reading an empty queue is not an error, it simply yields no bytes.
    result = queue.read(buffer, sizeof(buffer));
    assert(result);
    assert(result.transferred == 0);
}

// 佇列滿了要捨棄新字元而不是覆蓋舊字元，且不得回報成功。
// A full queue drops the new character rather than overwriting an old one, and
// must not report success.
void test_queue_overflow() {
    shirley::io::InputQueue queue;
    const std::size_t usable = shirley::io::InputQueue::capacity - 1;
    for (std::size_t i = 0; i < usable; ++i) assert(queue.push('x'));
    assert(queue.available() == usable);
    assert(!queue.push('y'));
    assert(queue.available() == usable);

    char first = '\0';
    assert(queue.read(&first, 1).transferred == 1);
    assert(first == 'x');
    // 讀走一個之後又有空間，環狀索引必須正確繞回。
    // One read frees a slot again, so the ring index has to wrap correctly.
    assert(queue.push('y'));
    assert(queue.available() == usable);
}

// 佇列只用於輸入，寫入必須被拒絕；空指標則是參數錯誤。
// The queue is input-only, so a write must be refused, and a null buffer is an
// argument error.
void test_queue_rejects_writes() {
    shirley::io::InputQueue queue;
    auto result = queue.write("x", 1);
    assert(!result);
    assert(result.error == shirley::io::Error::Unsupported);
    result = queue.read(nullptr, 4);
    assert(!result);
    assert(result.error == shirley::io::Error::InvalidArgument);
}

// 佇列本身就是位元組串流，可以直接接成標準輸入。
// The queue is itself a byte stream, so it can serve as standard input
// directly.
void test_queue_as_standard_input() {
    shirley::io::InputQueue queue;
    shirley::io::set_standard_input(&queue);
    assert(shirley::io::standard_input() == &queue);
    assert(queue.push('k'));
    char value = '\0';
    const auto result = shirley::io::read_standard_input(&value, 1);
    assert(result);
    assert(result.transferred == 1);
    assert(value == 'k');
    shirley::io::set_standard_input(nullptr);
    assert(!shirley::io::read_standard_input(&value, 1));
}

} // namespace

int main() {
    test_release_flag();
    test_ascii_table();
    test_decoder_ignores_releases();
    test_decoder_extended_keys();
    test_decoder_typed_word();
    test_queue_round_trip();
    test_queue_overflow();
    test_queue_rejects_writes();
    test_queue_as_standard_input();
    return 0;
}
