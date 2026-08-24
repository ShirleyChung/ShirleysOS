#include "shirley/console.hpp"
namespace shirley::console {
// QEMU virt 平台上的 PL011 UART 基底位址。
static volatile unsigned int* const uart = reinterpret_cast<volatile unsigned int*>(0x09000000);
// 設定 UART 為可傳送字元的基本模式。
void initialize() { uart[12] = 0; uart[11] = (3u << 5); uart[12] = (3u << 5) | (1u << 4) | (1u << 0); }
// 等待 UART 有空間後逐字輸出；換行時補上回車符。
void write(const char* text, std::size_t length) { for (std::size_t i = 0; i < length; ++i) { while (uart[6] & (1u << 5)) {} if (text[i] == '\n') uart[0] = '\r'; uart[0] = static_cast<unsigned char>(text[i]); } }
// 計算字串長度後交給長度版本輸出。
void write(const char* text) { if (!text) return; std::size_t n = 0; while (text[n]) ++n; write(text, n); }
}
