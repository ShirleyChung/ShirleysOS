#include "shirley/arch.hpp"
namespace shirley::arch {
void initialize() {}
const char* name() { return "ARM64"; }
[[noreturn]] void halt() { for (;;) { asm volatile("wfe"); } }
}
