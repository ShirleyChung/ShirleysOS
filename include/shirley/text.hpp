#pragma once

#include <cstddef>

// 核心沒有 libc，但檔案系統與 shell 都要處理字串，因此把少量共用的字串操作
// 集中在這裡。每個函式都以緩衝區容量為界，容納不下時回傳 false 而不是截斷：
// 被截斷的路徑會安靜地指向另一個檔案，那比明確的失敗危險得多。
//
// The kernel has no libc, yet both the file system and the shell handle
// strings, so the few shared string operations live here. Every function is
// bounded by the destination capacity and returns false rather than truncating
// when something does not fit: a truncated path quietly names a different file,
// which is far more dangerous than an explicit failure.
namespace shirley::text {

std::size_t length(const char* text);
bool equals(const char* left, const char* right);
// 只比較前 count 個字元；任一邊提前結束就視為不同。
// Compare only the first count characters; either side ending early makes the
// two different.
bool equals(const char* left, const char* right, std::size_t count);

// 複製字串並保證結尾為 null。容納不下時目的地會變成空字串並回傳 false。
// Copy a string, always null-terminated. When it does not fit, the destination
// becomes an empty string and the call returns false.
bool copy(char* destination, std::size_t capacity, const char* source);
// 附加字串。容納不下時目的地維持原樣並回傳 false。
// Append a string. When it does not fit, the destination is left unchanged and
// the call returns false.
bool append(char* destination, std::size_t capacity, const char* source);
bool append(char* destination, std::size_t capacity, char value);

// UTF-16 與 UTF-8 之間的轉換，供 exFAT 與 NTFS 的檔名使用。
//
// 磁碟上的 UTF-16 名稱不保證合法：Windows 允許落單的代理字元。這類字元以
// WTF-8 的方式編成三個位元組，而不是換成 U+FFFD，因此兩個方向互為反函數，
// 列出來的名稱一定查得回同一個檔案。
//
// Conversion between UTF-16 and UTF-8, for exFAT and NTFS file names.
//
// A UTF-16 name on disk is not guaranteed to be well formed: Windows allows a
// lone surrogate. Such a unit is encoded in three bytes the WTF-8 way rather
// than replaced by U+FFFD, so the two directions are exact inverses and a name
// that was listed always looks up the same file again.

// 把 count 個 UTF-16 單元轉成以 null 結尾的 UTF-8。放不下時目的地變成空字串
// 並回傳 false。
//
// Convert count UTF-16 units into null-terminated UTF-8. When the result does
// not fit, the destination becomes an empty string and the call returns false.
bool utf16_to_utf8(const char16_t* source, std::size_t count, char* destination,
                   std::size_t capacity);
// 把以 null 結尾的 UTF-8 轉成 UTF-16，回傳單元數。輸入不是合法的 UTF-8（或
// WTF-8），或超過 capacity 個單元時回傳 false。
//
// Convert null-terminated UTF-8 into UTF-16 and report how many units it took.
// Returns false when the input is not valid UTF-8 (or WTF-8) or needs more than
// capacity units.
bool utf8_to_utf16(const char* source, char16_t* destination, std::size_t capacity,
                   std::size_t& count);

} // namespace shirley::text
