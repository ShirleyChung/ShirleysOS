#include "internal.hpp"

#include "shirley/console.hpp"
#include "shirley/format.hpp"

namespace shirley::arch::arm64 {
namespace {

InterruptHandler handlers[vector_table_size]{};
void* contexts[vector_table_size]{};

const char* vector_name(std::uint64_t vector) {
    static const char* const names[vector_table_size] = {
        "current EL with SP0: synchronous", "current EL with SP0: IRQ",
        "current EL with SP0: FIQ", "current EL with SP0: SError",
        "current EL with SPx: synchronous", "current EL with SPx: IRQ",
        "current EL with SPx: FIQ", "current EL with SPx: SError",
        "lower EL (AArch64): synchronous", "lower EL (AArch64): IRQ",
        "lower EL (AArch64): FIQ", "lower EL (AArch64): SError",
        "lower EL (AArch32): synchronous", "lower EL (AArch32): IRQ",
        "lower EL (AArch32): FIQ", "lower EL (AArch32): SError"};
    return vector < vector_table_size ? names[vector] : "unknown vector";
}

// ESR_EL1 的 EC 欄位說明例外原因，這裡只列出開機階段常見的幾種。
// The EC field of ESR_EL1 gives the exception cause; only the ones common
// during bring-up are decoded here.
const char* exception_class_name(std::uint64_t esr) {
    switch ((esr >> 26) & 0x3f) {
        case 0x00: return "unknown reason";
        case 0x0e: return "illegal execution state";
        case 0x15: return "SVC from AArch64";
        case 0x18: return "trapped MSR/MRS";
        case 0x20: case 0x21: return "instruction abort";
        case 0x24: case 0x25: return "data abort";
        case 0x22: return "PC alignment fault";
        case 0x26: return "SP alignment fault";
        case 0x30: case 0x31: return "breakpoint";
        case 0x3c: return "BRK instruction";
        default: return "other";
    }
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
[[noreturn]] void report(const ExceptionFrame& frame) {
    console::write("\n*** ShirleyOS CPU exception: ");
    console::write(vector_name(frame.vector));
    console::write("\n    cause      ");
    console::write(exception_class_name(frame.esr));
    write_hex_field("\n    esr        ", frame.esr);
    write_hex_field("\n    elr        ", frame.elr);
    write_hex_field("\n    spsr       ", frame.spsr);
    write_hex_field("\n    far        ", frame.fault_address);
    write_hex_field("\n    sp         ", frame.stack_pointer);
    console::write("\n");
    for (;;) asm volatile("msr daifset, #0xf; wfi");
}

} // namespace

void exception_initialize() {
    for (unsigned vector = 0; vector < vector_table_size; ++vector) {
        handlers[vector] = nullptr;
        contexts[vector] = nullptr;
    }
    // VBAR_EL1 指向向量表基底；isb 確保後續例外立即使用新表。
    // VBAR_EL1 points at the table base; the isb makes later exceptions use it
    // immediately.
    asm volatile("msr vbar_el1, %0; isb" : : "r"(arm64_exception_vectors) : "memory");
}

bool exception_register(unsigned vector, InterruptHandler handler, void* context) {
    if (vector >= vector_table_size || handler == nullptr) return false;
    // 先寫 context 再寫 handler，例外不會看到搭配錯誤的組合。
    // Store the context before the handler so an exception can never observe a
    // mismatched pair.
    contexts[vector] = context;
    handlers[vector] = handler;
    return true;
}

void exception_unregister(unsigned vector) {
    if (vector >= vector_table_size) return;
    handlers[vector] = nullptr;
    contexts[vector] = nullptr;
}

} // namespace shirley::arch::arm64

// 由 exception.S 呼叫；frame 指向堆疊上的暫存器影像。
// Called from exception.S; frame points at the register image on the stack.
extern "C" void arm64_exception_dispatch(shirley::arch::arm64::ExceptionFrame* frame) {
    using namespace shirley::arch::arm64;
    const auto vector = frame->vector;
    if (vector < vector_table_size && handlers[vector] != nullptr) {
        handlers[vector](static_cast<unsigned>(vector), contexts[vector]);
        return;
    }
    report(*frame);
}
