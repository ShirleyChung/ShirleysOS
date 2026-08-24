#pragma once

#include <cstdint>

namespace shirley::arch {

// 架構相關資源的 opaque handle。
using AddressSpaceHandle = std::uintptr_t;

// 初始化處理器與架構層資源。
void initialize();
// 取得架構名稱。
const char* name();
void enable_interrupts();
void disable_interrupts();
[[noreturn]] void halt();
// 取得目前核心堆疊指標。
std::uintptr_t current_stack_pointer();
void switch_address_space(AddressSpaceHandle);
[[noreturn]] void enter_userspace(std::uintptr_t entry, std::uintptr_t user_stack);

} // namespace shirley::arch
