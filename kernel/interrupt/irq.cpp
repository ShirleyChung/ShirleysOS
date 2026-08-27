#include "shirley/irq.hpp"

#include "shirley/arch.hpp"
#include "shirley/platform.hpp"

namespace shirley::irq {
namespace {

struct Entry {
    Handler handler = nullptr;
    void* context = nullptr;
    unsigned irq = 0;
    bool taken = false;
    std::uint64_t dispatches = 0;
};

Entry entries[max_irq_count]{};

// 架構向量處理常式。context 指向該 IRQ 自己的項目，因此不必再從向量反推
// IRQ 編號，也就不必假設向量與 IRQ 之間是哪一種對應關係。
//
// The architecture vector handler. Its context points at the IRQ's own entry,
// so the IRQ number never has to be derived back out of the vector, and no
// assumption about the vector-to-IRQ relationship is baked in here.
void vector_trampoline(unsigned, void* context) {
    dispatch(static_cast<Entry*>(context)->irq);
}

} // namespace

void initialize() {
    for (unsigned irq = 0; irq < max_irq_count; ++irq) entries[irq] = Entry{};
}

bool request(unsigned irq, Handler handler, void* context) {
    if (irq >= max_irq_count || handler == nullptr) return false;
    auto& entry = entries[irq];
    if (entry.taken) return false;

    entry.irq = irq;
    entry.context = context;
    entry.handler = handler;
    entry.dispatches = 0;
    // 先讓向量指向本層，再解除控制器遮罩，中斷才不會落在還沒接好的向量上。
    // Point the vector at this layer before unmasking the controller, so an
    // interrupt cannot arrive on a vector that is not wired up yet.
    if (!arch::register_interrupt_handler(platform::irq_vector(irq), vector_trampoline, &entry)) {
        entry = Entry{};
        return false;
    }
    entry.taken = true;
    platform::enable_irq(irq);
    return true;
}

void release(unsigned irq) {
    if (irq >= max_irq_count) return;
    auto& entry = entries[irq];
    if (!entry.taken) return;
    // 先遮罩再拆掉向量，順序與 request() 相反。
    // Mask first and unhook the vector second, the reverse of request().
    platform::disable_irq(irq);
    arch::unregister_interrupt_handler(platform::irq_vector(irq));
    entry = Entry{};
}

bool registered(unsigned irq) { return irq < max_irq_count && entries[irq].taken; }

void dispatch(unsigned irq) {
    if (irq >= max_irq_count) return;
    // 假中斷不執行處理常式，也不送 end-of-interrupt：控制器並未真的進入
    // 服務中狀態，多送一次 EOI 會把下一個真正的中斷提早結束掉。
    //
    // A spurious interrupt runs no handler and gets no end-of-interrupt: the
    // controller never actually entered the in-service state, and an extra EOI
    // would prematurely finish the next genuine interrupt instead.
    if (platform::spurious_interrupt(irq)) return;

    auto& entry = entries[irq];
    ++entry.dispatches;
    if (entry.taken && entry.handler != nullptr) entry.handler(irq, entry.context);
    // 即使沒有處理常式也要送出 EOI，否則控制器會停在服務中狀態，
    // 之後同優先權的中斷都不會再送達。
    //
    // Send the EOI even with no handler installed. Otherwise the controller
    // stays in service and no further interrupt at that priority is delivered.
    platform::end_of_interrupt(irq);
}

std::uint64_t count(unsigned irq) { return irq < max_irq_count ? entries[irq].dispatches : 0; }

} // namespace shirley::irq
