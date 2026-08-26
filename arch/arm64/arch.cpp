#include "internal.hpp"

namespace shirley::arch {

// 開機組合語言只建立堆疊；CPU 功能與例外向量表在這裡接手。
// The boot assembly only establishes a stack. CPU features and the exception
// vector table are set up here.
void initialize() {
    arm64::cpu_initialize();
    arm64::exception_initialize();
}

// 回傳供核心訊息與診斷使用的架構名稱。
// The architecture name used in kernel messages and diagnostics.
const char* name() { return "ARM64"; }
const char* cpu_vendor() { return arm64::cpu_vendor_string(); }

// DAIF 的 I 位元遮罩 IRQ；daifclr/daifset 直接操作該位元。
// The I bit in DAIF masks IRQs; daifclr and daifset act on it directly.
void enable_interrupts() { asm volatile("msr daifclr, #2" : : : "memory"); }
void disable_interrupts() { asm volatile("msr daifset, #2" : : : "memory"); }

bool interrupts_enabled() {
    std::uint64_t state;
    asm volatile("mrs %0, daif" : "=r"(state));
    return (state & (1ull << 7)) == 0;
}

// WFI 會在下一個中斷到來時返回。
// WFI returns as soon as the next interrupt arrives.
void wait_for_interrupt() { asm volatile("wfi"); }

// 遮罩所有例外後以 WFI 永久停住處理器。
// Mask every exception and park the processor in WFI for good.
[[noreturn]] void halt() { for (;;) asm volatile("msr daifset, #0xf; wfi"); }

std::uintptr_t current_stack_pointer() {
    std::uintptr_t stack_pointer;
    asm volatile("mov %0, sp" : "=r"(stack_pointer));
    return stack_pointer;
}

AddressSpaceHandle current_address_space() {
    std::uint64_t value;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(value));
    return static_cast<AddressSpaceHandle>(value);
}

// 換掉使用者轉換表後必須讓對應的 TLB 項目失效。
// After swapping the user translation table its TLB entries must be flushed.
void switch_address_space(AddressSpaceHandle handle) {
    asm volatile("msr ttbr0_el1, %0; isb; tlbi vmalle1is; dsb ish; isb"
                 : : "r"(static_cast<std::uint64_t>(handle)) : "memory");
}

// EL0 使用 SP_EL0，因此核心堆疊只需保留在 SP_EL1 中，不需額外設定。
// EL0 runs on SP_EL0, so the kernel stack simply stays in SP_EL1 and needs no
// separate configuration.
void set_kernel_stack(std::uintptr_t) {}

[[noreturn]] void enter_userspace(std::uintptr_t entry, std::uintptr_t user_stack) {
    arm64_enter_userspace(entry, user_stack);
}

// ARM64 的 vector 為例外向量表入口編號，定義於 shirley/arch/arm64/exception.hpp。
// On ARM64 a vector is an exception vector table entry number, defined in
// shirley/arch/arm64/exception.hpp.
bool register_interrupt_handler(unsigned vector, InterruptHandler handler, void* context) {
    return arm64::exception_register(vector, handler, context);
}

void unregister_interrupt_handler(unsigned vector) { arm64::exception_unregister(vector); }

} // namespace shirley::arch
