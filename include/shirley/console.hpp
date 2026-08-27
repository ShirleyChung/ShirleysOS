#pragma once

#include <cstddef>

namespace shirley::console {

// 可替換的主控台輸出後端。核心只依賴這個小介面，平台或測試可以在
// initialize() 前換成自己的實作。
// A replaceable console backend. The kernel depends only on this small
// interface; a platform or test may install its own implementation before
// initialize().
class Backend {
public:
    virtual void initialize() = 0;
    virtual void write(const char* text, std::size_t length) = 0;

protected:
    ~Backend() = default;
};

// 設定目前後端；傳入 nullptr 會恢復平台預設後端。
// Select the active backend; nullptr restores the platform default.
void set_backend(Backend* backend);
Backend* backend();

// 每個平台提供自己的預設後端。host 平台的預設值是 shell console。
// Each platform supplies its default backend. The host default is the shell
// console.
Backend* default_backend();

// 初始化目前平台的主控台輸出裝置。
// Initialize the current platform's console output device.
void initialize();
// 輸出以 null 結尾的字串。
// Write a null-terminated string.
void write(const char* text);
// 輸出指定長度的位元組序列。
// Write a byte sequence of the given length.
void write(const char* text, std::size_t length);

} // namespace shirley::console
