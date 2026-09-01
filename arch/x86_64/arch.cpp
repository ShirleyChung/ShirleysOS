#include "internal.hpp"

namespace shirley::arch {
namespace {

// 使用者行程做系統呼叫時，ring 3 -> ring 0 的中斷會從 TSS 載入 RSP0，因此
// 需要一個核心堆疊。它獨立於啟動核心行程的那條呼叫鏈，這樣系統呼叫處理常式
// 就不會覆寫保存在該堆疊上的 UserContext。它是 .bss，因此已被 launch 的
// map_kernel 映射進每個使用者位址空間。
//
// A user process's syscall takes a ring 3 -> ring 0 interrupt that loads RSP0
// from the TSS, so it needs a kernel stack. It is separate from the call chain
// that started the process so a syscall handler never overwrites the
// UserContext saved on that stack. Being .bss, launch's map_kernel already maps
// it into every user address space.
alignas(16) unsigned char syscall_stacks[2][16 * 1024];
unsigned user_depth = 0;

// 目前正在執行的使用者行程保存的核心狀態；exit_userspace 用它跳回去。
// The saved kernel state of the running user process; exit_userspace uses it to
// jump back.
x86_64::UserContext* user_context = nullptr;

} // namespace

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
const char* interrupt_table_name() { return "IDT"; }

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

// 以 IRET 從 Ring 0 切換到 Ring 3，並在行程 exit 時返回其結束碼。
// Drop from ring 0 to ring 3 with IRET, returning the process's exit status
// when it calls exit.
int enter_userspace(std::uintptr_t entry, std::uintptr_t user_stack) {
    x86_64::UserContext context{};
    auto* previous = user_context;
    user_context = &context;
    // 系統呼叫的核心堆疊指向獨立的 syscall_stack，而不是目前這條呼叫鏈，
    // 保存在此的 context 才不會被覆寫。
    //
    // Point the syscall kernel stack at the separate syscall_stack rather than
    // this call chain, so the context saved here is never overwritten.
    const unsigned stack_index = user_depth < 2 ? user_depth : 1;
    ++user_depth;
    x86_64::gdt_set_kernel_stack(reinterpret_cast<std::uintptr_t>(syscall_stacks[stack_index]) + sizeof(syscall_stacks[stack_index]));
    const long status = x86_64_save_and_enter(entry, user_stack, x86_64::user_code_selector,
                                              x86_64::user_data_selector, &context);
    user_context = previous;
    --user_depth;
    if (user_depth != 0) {
        const unsigned parent = user_depth - 1 < 2 ? user_depth - 1 : 1;
        x86_64::gdt_set_kernel_stack(reinterpret_cast<std::uintptr_t>(syscall_stacks[parent]) + sizeof(syscall_stacks[parent]));
    }
    return static_cast<int>(status);
}

// 由 exit 系統呼叫呼叫；跳回 enter_userspace 保存的核心狀態並讓它返回 status。
// Called by the exit syscall; jumps back to the kernel state enter_userspace
// saved and makes it return status.
[[noreturn]] void exit_userspace(int status) {
    x86_64_restore_and_exit(user_context, static_cast<long>(status));
}

// x86_64 的 vector 就是 IDT 向量編號 0-255。
// On x86_64 a vector is simply an IDT vector number, 0-255.
bool register_interrupt_handler(unsigned vector, InterruptHandler handler, void* context) {
    return x86_64::idt_register(vector, handler, context);
}

void unregister_interrupt_handler(unsigned vector) { x86_64::idt_unregister(vector); }

} // namespace shirley::arch
