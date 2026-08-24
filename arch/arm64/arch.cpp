#include "shirley/arch.hpp"
namespace shirley::arch {
// ARM64 的早期初始化目前由啟動組合語言負責。
void initialize() {}
// 回傳供核心訊息與診斷使用的架構名稱。
const char* name() { return "ARM64"; }
// 以 WFE 低功耗等待指令停住目前處理器。
[[noreturn]] void halt() { for (;;) { asm volatile("wfe"); } }
}
