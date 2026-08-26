#include "internal.hpp"

#include "shirley/console.hpp"
#include "shirley/format.hpp"

namespace shirley::arch::x86_64 {
namespace {

// 中斷閘描述元，位址被拆成三段存放。
// An interrupt gate descriptor; the handler address is split into three fields.
struct [[gnu::packed]] Gate {
    std::uint16_t offset_low;
    std::uint16_t selector;
    std::uint8_t ist;
    std::uint8_t flags;
    std::uint16_t offset_mid;
    std::uint32_t offset_high;
    std::uint32_t reserved;
};

struct [[gnu::packed]] DescriptorPointer {
    std::uint16_t limit;
    std::uint64_t base;
};

alignas(16) Gate gates[vector_count]{};
DescriptorPointer pointer{};
InterruptHandler handlers[vector_count]{};
void* contexts[vector_count]{};

// 從 interrupt.S 的頭尾符號反推每個 ISR 入口佔用的位元組數。
// Derive the size of one ISR entry from interrupt.S's start and end symbols.
std::size_t stub_stride() {
    return static_cast<std::size_t>(x86_64_isr_stubs_end - x86_64_isr_stubs) / vector_count;
}

// 前 32 個向量是 CPU 例外，名稱只用於診斷輸出。
// Vectors 0-31 are CPU exceptions; these names are only for diagnostics.
const char* exception_name(std::uint64_t vector) {
    static const char* const names[32] = {
        "divide error", "debug", "non-maskable interrupt", "breakpoint",
        "overflow", "bound range exceeded", "invalid opcode", "device not available",
        "double fault", "coprocessor segment overrun", "invalid TSS", "segment not present",
        "stack-segment fault", "general protection fault", "page fault", "reserved",
        "x87 floating-point error", "alignment check", "machine check", "SIMD floating-point error",
        "virtualization exception", "control protection exception", "reserved", "reserved",
        "reserved", "reserved", "reserved", "hypervisor injection",
        "VMM communication exception", "security exception", "reserved", "reserved"};
    return vector < 32 ? names[vector] : "interrupt";
}

void write_hex_field(const char* label, std::uint64_t value) {
    char buffer[19];
    console::write(label);
    console::write("0x");
    format::to_hex(buffer, sizeof(buffer), value, 16);
    console::write(buffer);
}

// 沒有註冊處理常式的例外會印出診斷後停機。
// An exception with no registered handler prints a register dump and stops.
[[noreturn]] void report_exception(const InterruptFrame& frame) {
    console::write("\n*** ShirleyOS CPU exception: ");
    console::write(exception_name(frame.vector));
    write_hex_field("\n    vector     ", frame.vector);
    write_hex_field("\n    error code ", frame.error_code);
    write_hex_field("\n    rip        ", frame.rip);
    write_hex_field("\n    rsp        ", frame.rsp);
    write_hex_field("\n    rflags     ", frame.rflags);
    if (frame.vector == 14) {
        // 分頁錯誤時 CR2 保存了造成錯誤的位址。
        // On a page fault CR2 holds the address that faulted.
        std::uint64_t faulting_address = 0;
        asm volatile("mov %%cr2, %0" : "=r"(faulting_address));
        write_hex_field("\n    cr2        ", faulting_address);
    }
    console::write("\n");
    for (;;) asm volatile("cli; hlt");
}

void set_gate(unsigned vector, std::uint64_t handler) {
    // type=0xe（中斷閘）、present=1、DPL=0；進入處理常式時自動關閉中斷。
    // type=0xe (interrupt gate), present=1, DPL=0. An interrupt gate clears IF
    // on entry to the handler.
    gates[vector].offset_low = static_cast<std::uint16_t>(handler);
    gates[vector].selector = kernel_code_selector;
    gates[vector].ist = 0;
    gates[vector].flags = 0x8e;
    gates[vector].offset_mid = static_cast<std::uint16_t>(handler >> 16);
    gates[vector].offset_high = static_cast<std::uint32_t>(handler >> 32);
    gates[vector].reserved = 0;
}

} // namespace

void idt_initialize() {
    const auto stubs = reinterpret_cast<std::uint64_t>(x86_64_isr_stubs);
    const auto stride = stub_stride();
    for (unsigned vector = 0; vector < vector_count; ++vector) {
        handlers[vector] = nullptr;
        contexts[vector] = nullptr;
        set_gate(vector, stubs + vector * stride);
    }
    pointer = {static_cast<std::uint16_t>(sizeof(gates) - 1), reinterpret_cast<std::uint64_t>(gates)};
    x86_64_load_idt(&pointer);
}

bool idt_register(unsigned vector, InterruptHandler handler, void* context) {
    if (vector >= vector_count || handler == nullptr) return false;
    // 先寫 context 再寫 handler，中斷不會看到搭配錯誤的組合。
    // Store the context before the handler so an interrupt can never observe a
    // mismatched pair.
    contexts[vector] = context;
    handlers[vector] = handler;
    return true;
}

void idt_unregister(unsigned vector) {
    if (vector >= vector_count) return;
    handlers[vector] = nullptr;
    contexts[vector] = nullptr;
}

} // namespace shirley::arch::x86_64

// 由 interrupt.S 呼叫；frame 指向堆疊上的暫存器影像。
// Called from interrupt.S; frame points at the register image on the stack.
extern "C" void x86_64_interrupt_dispatch(shirley::arch::x86_64::InterruptFrame* frame) {
    using namespace shirley::arch;
    using namespace shirley::arch::x86_64;
    const auto vector = frame->vector;
    if (vector < vector_count && handlers[vector] != nullptr) {
        handlers[vector](static_cast<unsigned>(vector), contexts[vector]);
        return;
    }
    // 未註冊的例外視為致命錯誤；未註冊的裝置中斷直接忽略。
    // An unregistered exception is fatal; an unregistered device interrupt is
    // simply ignored.
    if (vector < 32) report_exception(*frame);
}
