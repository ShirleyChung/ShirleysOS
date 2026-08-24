#pragma once

#include <cstddef>

namespace shirley::console {

void initialize();
void write(const char* text);
void write(const char* text, std::size_t length);

} // namespace shirley::console
