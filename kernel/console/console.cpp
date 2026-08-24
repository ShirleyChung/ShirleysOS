#include "shirley/console.hpp"

#include <cstdio>

namespace shirley::console {
void initialize() {}
void write(const char* text) { if (text) std::fputs(text, stdout); }
void write(const char* text, std::size_t length) { if (text) std::fwrite(text, 1, length, stdout); }
} // namespace shirley::console
