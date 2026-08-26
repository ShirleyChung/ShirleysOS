#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley::arch {

// 架構相關位址空間的 opaque handle（x86_64 為 CR3，ARM64 為 TTBR0）。
// Opaque handle for an architecture address space (CR3 on x86_64, TTBR0 on ARM64).
using AddressSpaceHandle = std::uintptr_t;

// 架構中斷向量數量，以及平台層掛載裝置中斷所用的處理常式型別。
// The architecture vector count and the handler type the platform layer
// installs device interrupts with.
constexpr unsigned vector_count = 256;
using InterruptHandler = void (*)(unsigned vector, void* context);

// 初始化處理器與架構層資源。
// Initialize the processor and the architecture layer's own resources.
void initialize();
// 取得架構名稱。
// The architecture name.
const char* name();
// 取得 CPU 廠商字串；架構無法辨識時回傳 "unknown"。
// The CPU vendor string, or "unknown" when the architecture cannot identify it.
const char* cpu_vendor();

void enable_interrupts();
void disable_interrupts();
bool interrupts_enabled();
// 讓處理器停在低功耗狀態直到下一個中斷。
// Park the processor in a low-power state until the next interrupt.
void wait_for_interrupt();
[[noreturn]] void halt();

// 取得目前核心堆疊指標。
// The current kernel stack pointer.
std::uintptr_t current_stack_pointer();
AddressSpaceHandle current_address_space();
void switch_address_space(AddressSpaceHandle);
// 設定特權層轉換時要使用的核心堆疊頂端。
// Set the kernel stack top used when a privilege-level transition occurs.
void set_kernel_stack(std::uintptr_t stack_top);
[[noreturn]] void enter_userspace(std::uintptr_t entry, std::uintptr_t user_stack);

// 註冊中斷向量處理常式；向量超出範圍或架構尚未支援時回傳 false。
// 向量編號的意義由各架構定義，詳見 OS_SPEC.md。
// Register a handler for an interrupt vector; returns false when the vector is
// out of range or the architecture does not support it yet. What a vector
// number means is defined per architecture; see OS_SPEC.md.
bool register_interrupt_handler(unsigned vector, InterruptHandler handler, void* context = nullptr);
void unregister_interrupt_handler(unsigned vector);

} // namespace shirley::arch
