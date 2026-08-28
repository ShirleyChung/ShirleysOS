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

} // namespace shirley::text
