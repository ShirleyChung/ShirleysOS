#include "shirley/arch.hpp"
namespace shirley::arch {
// x86_64 的模式切換已在 boot.S 完成，這裡提供統一初始化介面。
void initialize() {}
// 回傳架構識別名稱。
const char* name() { return "x86_64"; }
// 以 HLT 指令停止處理器。
[[noreturn]] void halt() { for (;;) { asm volatile("hlt"); } }
}
