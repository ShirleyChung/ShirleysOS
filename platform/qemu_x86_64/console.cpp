#include "shirley/console.hpp"
namespace shirley::console {
// 透過 x86 I/O port 存取傳統 PC COM1 序列埠。
static inline void outb(unsigned short port, unsigned char value) { asm volatile("outb %0,%1" : : "a"(value), "Nd"(port)); }
// 設定 COM1 的鮑率、資料格式與 FIFO。
void initialize() { outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 3); outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 7); outb(0x3fc, 3); }
// 將字元送到 COM1；換行時先送回車符。
void write(const char* text, std::size_t length) { for (std::size_t i = 0; i < length; ++i) { if (text[i] == '\n') outb(0x3f8, '\r'); outb(0x3f8, text[i]); } }
// 計算 null 結尾字串長度後輸出。
void write(const char* text) { if (!text) return; std::size_t n = 0; while (text[n]) ++n; write(text, n); }
}
