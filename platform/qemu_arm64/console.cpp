#include "shirley/console.hpp"
namespace shirley::console {
static volatile unsigned int* const uart = reinterpret_cast<volatile unsigned int*>(0x09000000);
void initialize() { uart[12] = 0; uart[11] = (3u << 5); uart[12] = (3u << 5) | (1u << 4) | (1u << 0); }
void write(const char* text, std::size_t length) { for (std::size_t i = 0; i < length; ++i) { while (uart[6] & (1u << 5)) {} if (text[i] == '\n') uart[0] = '\r'; uart[0] = static_cast<unsigned char>(text[i]); } }
void write(const char* text) { if (!text) return; std::size_t n = 0; while (text[n]) ++n; write(text, n); }
}
