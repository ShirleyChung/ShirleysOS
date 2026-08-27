#include "shirley/platform/arm/generic_timer.hpp"

#include "shirley/console.hpp"
#include "shirley/irq.hpp"

namespace shirley::platform::arm {
namespace {

// CNTP_CTL_EL0 的控制位元。ENABLE 讓計時器開始比較，IMASK 會擋住中斷輸出，
// ISTATUS 則是唯讀的「條件已成立」旗標。
//
// The control bits of CNTP_CTL_EL0. ENABLE starts the comparison, IMASK holds
// the interrupt output back, and ISTATUS is the read-only "condition met"
// flag.
constexpr std::uint64_t control_enable = 1ull << 0;
constexpr std::uint64_t control_interrupt_mask = 1ull << 1;

// tick 由中斷處理常式更新，主線程只讀取，因此標記為 volatile。
// The tick count is written by the interrupt handler and only read by the main
// flow, so it is marked volatile.
volatile std::uint64_t ticks = 0;
unsigned configured_frequency = 0;
std::uint64_t interval = 0;

std::uint64_t counter_frequency() {
    std::uint64_t value = 0;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

// TVAL 是一個往下數的計數器，寫入它等於「從現在起再過這麼多個計數」。
// TVAL counts down, so writing it means "this many counts from now".
void arm_next_interval() {
    asm volatile("msr cntp_tval_el0, %0" : : "r"(interval));
}

void set_control(std::uint64_t value) {
    asm volatile("msr cntp_ctl_el0, %0; isb" : : "r"(value) : "memory");
}

// 計時器中斷除了累加 tick 之外只做一件事：重新設定下一次的間隔。計時器的
// 中斷輸出是位準訊號，不重新設定條件就會一直成立，處理常式會被無止盡地
// 重新進入。
//
// Besides counting, the timer interrupt does exactly one thing: it arms the
// next interval. The timer's interrupt output is a level, so leaving the
// condition met would re-enter the handler forever.
void timer_interrupt(unsigned, void*) {
    arm_next_interval();
    ticks = ticks + 1;
}

} // namespace

bool generic_timer_initialize(unsigned frequency) {
    ticks = 0;
    configured_frequency = 0;
    // 先停掉計時器，設定期間才不會有中斷送出。
    // Stop the timer first so nothing is delivered while it is configured.
    set_control(control_interrupt_mask);

    const auto counter = counter_frequency();
    if (counter == 0 || frequency == 0) return false;
    interval = counter / frequency;
    if (interval == 0) interval = 1;
    // 實際頻率由間隔反推，因此回報的是硬體真正在跑的速率。
    // The actual rate is derived back from the interval, so what is reported
    // is the rate the hardware really runs at.
    configured_frequency = static_cast<unsigned>(counter / interval);

    arm_next_interval();
    // 先讓計時器跑起來但擋住中斷輸出，等 IRQ 層接好之後才放行。
    // Start the timer with its interrupt output still held back, and release
    // it only once the IRQ layer is wired up.
    set_control(control_enable | control_interrupt_mask);

    if (!irq::request(generic_timer_irq, timer_interrupt)) {
        set_control(control_interrupt_mask);
        console::write("[IRQ] architected timer PPI 30 registration failed\n");
        return false;
    }
    set_control(control_enable);
    console::write("[IRQ] architected timer enabled on PPI 30\n");
    return true;
}

std::uint64_t generic_timer_ticks() { return ticks; }
unsigned generic_timer_frequency() { return configured_frequency; }

} // namespace shirley::platform::arm
