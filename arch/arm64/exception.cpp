#include "internal.hpp"

#include "shirley/console.hpp"
#include "shirley/format.hpp"
#include "shirley/syscall.hpp"

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
    // 系統呼叫是使用者程式在 EL0（AArch64）執行 svc 進來的，只會落在
    // lower_el_aarch64_sync 這個入口。判斷必須以向量入口為準，不能只看 ESR 的
    // 例外分類：ESR_EL1 只在同步例外時更新，IRQ 進來時它仍保留上一次同步例外
    // 的值。核心在 user 行程結束後會繼續執行並持續收到計時器 IRQ，若只憑
    // ESR，那個殘留的 0x15（SVC）就會讓 IRQ 被誤當成系統呼叫、never EOI，於是
    // 卡在無止盡的中斷風暴裡。確認過向量入口後，再用 ESR 分類確認確實是 svc。
    //
    // A syscall enters when a user program at EL0 (AArch64) executes svc, which
    // only ever lands on the lower_el_aarch64_sync entry. The vector entry, not
    // ESR's exception class, is the authoritative test: ESR_EL1 is updated only
    // on a synchronous exception and keeps its previous value when an IRQ
    // arrives. The kernel now keeps running after a user process exits and goes
    // on taking timer IRQs, so relying on ESR would let that stale 0x15 (SVC)
    // make an IRQ look like a syscall that is never EOI'd, wedging the machine
    // in an endless interrupt storm. With the vector confirmed, ESR's class
    // confirms it really is an svc.
    if (vector == lower_el_aarch64_sync && ((frame->esr >> 26) & 0x3f) == 0x15) {
        shirley::syscall::Context context{
            frame->x[8],
            {frame->x[0], frame->x[1], frame->x[2]},
            -1};
        shirley::syscall::dispatch(context);
        frame->x[0] = static_cast<std::uint64_t>(context.result);
        return;
    }
    // 中斷不會因為它打斷的是 EL0 還是 EL1 而變成另一個中斷：同一條 IRQ 線、
    // 同一個裝置、同一個處理常式。硬體卻用不同的向量入口回報這兩種情形，
    // 因此把來自較低例外層級的 IRQ 與 FIQ 對應回目前例外層級的入口，驅動
    // 程式只需要註冊一次。同步例外與 SError 不這樣處理：user 行程的錯誤
    // 和核心自己的錯誤是兩回事，不該共用處理常式。
    //
    // An interrupt does not become a different interrupt because of which
    // exception level it interrupted: same line, same device, same handler.
    // The hardware nonetheless reports the two cases through different vector
    // entries, so a lower-EL IRQ or FIQ is mapped back onto the current-EL
    // entry and a driver registers only once. Synchronous exceptions and
    // SErrors are deliberately left alone: a fault in a user process and a
    // fault in the kernel are not the same event and must not share a handler.
    auto slot = vector;
    if (slot == lower_el_aarch64_irq) slot = current_el_spx_irq;
    if (slot == lower_el_aarch64_fiq) slot = current_el_spx_fiq;
    if (slot < vector_table_size && handlers[slot] != nullptr) {
        handlers[slot](static_cast<unsigned>(slot), contexts[slot]);
        return;
    }
    report(*frame);
}
