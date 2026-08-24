#include "shirley/arch.hpp"
namespace shirley::arch {
void initialize() {}
const char* name() { return "x86_64"; }
[[noreturn]] void halt() { for (;;) { asm volatile("hlt"); } }
}
