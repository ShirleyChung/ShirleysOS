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
// 架構中斷向量表的名稱，只用於開機診斷輸出：x86_64 是 "IDT"，
// ARM64 是 "EL1 exception vector table"。
//
// The name of the architecture's interrupt vector table, used only in boot
// diagnostics: "IDT" on x86_64 and "EL1 exception vector table" on ARM64.
const char* interrupt_table_name();

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

// 進入使用者模式執行 entry，直到該行程呼叫 exit 系統呼叫為止，並回傳它交出的
// 結束碼。這是一個會返回的函式：進入前先保存核心的執行狀態，exit 時再從那裡
// 接續，就像 setjmp/longjmp 那樣把控制權交回啟動它的核心程式碼。
//
// Enter user mode at entry and run until the process calls the exit syscall,
// then return the status it handed back. This is a function that returns: the
// kernel's execution state is saved before entering and resumed on exit, in
// the manner of setjmp/longjmp, handing control back to the kernel code that
// started the process.
int enter_userspace(std::uintptr_t entry, std::uintptr_t user_stack);

// 結束目前的使用者行程，讓對應的 enter_userspace() 以 status 作為回傳值返回。
// 由 exit 系統呼叫在中斷／例外處理常式內呼叫，因此它不會返回呼叫端，而是跳回
// 保存好的核心狀態。
//
// End the running user process, making the matching enter_userspace() return
// status. Called by the exit syscall from inside the interrupt/exception
// handler, so it does not return to its caller but longjmps back to the saved
// kernel state.
[[noreturn]] void exit_userspace(int status);

// 註冊中斷向量處理常式；向量超出範圍或架構尚未支援時回傳 false。
// 向量編號的意義由各架構定義，詳見 OS_SPEC.md。
// Register a handler for an interrupt vector; returns false when the vector is
// out of range or the architecture does not support it yet. What a vector
// number means is defined per architecture; see OS_SPEC.md.
bool register_interrupt_handler(unsigned vector, InterruptHandler handler, void* context = nullptr);
void unregister_interrupt_handler(unsigned vector);

} // namespace shirley::arch
