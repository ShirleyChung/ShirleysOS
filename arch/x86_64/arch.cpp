#include "internal.hpp"

namespace shirley::arch {

// 開機載入器只把處理器帶進長模式；GDT、TSS、IDT 與 CPU 功能在這裡接手。
// The boot loader only gets the processor into long mode. The GDT, TSS, IDT,
// and CPU features are set up here.
void initialize() {
    x86_64::cpu_initialize();
    x86_64::gdt_initialize();
    x86_64::idt_initialize();
}

// 回傳架構識別名稱。
// The architecture identifier.
const char* name() { return "x86_64"; }
const char* cpu_vendor() { return x86_64::cpu_vendor_string(); }

void enable_interrupts() { asm volatile("sti" : : : "memory"); }
void disable_interrupts() { asm volatile("cli" : : : "memory"); }

bool interrupts_enabled() {
    std::uint64_t flags;
    asm volatile("pushfq; pop %0" : "=r"(flags) : : "memory");
    // RFLAGS 的位元 9 是 IF（中斷啟用旗標）。
    // Bit 9 of RFLAGS is IF, the interrupt enable flag.
    return (flags & (1ull << 9)) != 0;
}

// HLT 會在下一個中斷到來時返回。
// HLT returns as soon as the next interrupt arrives.
void wait_for_interrupt() { asm volatile("hlt"); }

// 以關閉中斷後的 HLT 永久停住處理器。
// Stop the processor for good: disable interrupts, then halt.
[[noreturn]] void halt() { for (;;) asm volatile("cli; hlt"); }

std::uintptr_t current_stack_pointer() {
    std::uintptr_t stack_pointer;
    asm volatile("mov %%rsp, %0" : "=r"(stack_pointer));
    return stack_pointer;
}

AddressSpaceHandle current_address_space() {
    std::uintptr_t value;
    asm volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

// 寫入 CR3 會一併換掉整個 TLB 中的非全域項目。
// Writing CR3 also flushes every non-global TLB entry.
void switch_address_space(AddressSpaceHandle handle) {
    asm volatile("mov %0, %%cr3" : : "r"(static_cast<std::uintptr_t>(handle)) : "memory");
}

void set_kernel_stack(std::uintptr_t stack_top) { x86_64::gdt_set_kernel_stack(stack_top); }

// 以 IRET 從 Ring 0 切換到 Ring 3。
// Drop from ring 0 to ring 3 with IRET.
[[noreturn]] void enter_userspace(std::uintptr_t entry, std::uintptr_t user_stack) {
    x86_64_enter_userspace(entry, user_stack, x86_64::user_code_selector, x86_64::user_data_selector);
}

// x86_64 的 vector 就是 IDT 向量編號 0-255。
// On x86_64 a vector is simply an IDT vector number, 0-255.
bool register_interrupt_handler(unsigned vector, InterruptHandler handler, void* context) {
    return x86_64::idt_register(vector, handler, context);
}

void unregister_interrupt_handler(unsigned vector) { x86_64::idt_unregister(vector); }

} // namespace shirley::arch
