#include "shirley/platform/pc/pit.hpp"

#include "shirley/arch/x86_64/port_io.hpp"
#include "shirley/console.hpp"
#include "shirley/irq.hpp"

namespace shirley::platform::pc {
namespace {

using arch::x86_64::outb;

// 通道 0 的計數埠與整顆晶片共用的模式命令埠。
// Channel 0's counter port and the mode command port shared by the chip.
constexpr std::uint16_t channel0_data = 0x40;
constexpr std::uint16_t mode_command = 0x43;
// 通道 0、先低位元組再高位元組、模式 2（除頻器）、二進位計數。
// Channel 0, low byte then high byte, mode 2 (rate generator), binary
// counting.
constexpr std::uint8_t channel0_rate_generator = 0x34;

// tick 由中斷處理常式更新，主線程只讀取，因此標記為 volatile。
// The tick count is written by the interrupt handler and only read by the main
// flow, so it is marked volatile.
volatile std::uint64_t ticks = 0;
unsigned configured_frequency = 0;

// 把要求的頻率換算成 16 位元分頻值。計數值 0 在硬體上代表 65536，
// 也就是最慢的 18.2 Hz。
//
// Convert the requested rate into the 16-bit divisor. A count of 0 means
// 65536 to the hardware, which is the slowest rate of 18.2 Hz.
std::uint16_t divisor_for(unsigned frequency) {
    if (frequency == 0) return 0;
    std::uint32_t divisor = pit_base_frequency / frequency;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xffff) divisor = 0;
    return static_cast<std::uint16_t>(divisor);
}

// 計時器中斷除了累加 tick 之外不做任何事。這裡刻意不輸出任何訊息：
// 每秒 100 次的日誌會把主控台完全淹沒。
//
// The timer interrupt does nothing but count. It deliberately logs nothing:
// a message a hundred times a second would drown the console completely.
void pit_interrupt(unsigned, void*) { ticks = ticks + 1; }

} // namespace

bool pit_initialize(unsigned frequency) {
    const auto divisor = divisor_for(frequency);
    // 實際頻率由分頻值反推，因此回報的是硬體真正在跑的速率。
    // The actual rate is derived back from the divisor, so what is reported is
    // the rate the hardware really runs at.
    const std::uint32_t effective = divisor == 0 ? 0xffff + 1 : divisor;
    configured_frequency = pit_base_frequency / effective;
    ticks = 0;

    outb(mode_command, channel0_rate_generator);
    outb(channel0_data, static_cast<std::uint8_t>(divisor & 0xff));
    outb(channel0_data, static_cast<std::uint8_t>(divisor >> 8));

    if (!irq::request(pit_irq, pit_interrupt)) {
        console::write("[IRQ] PIT IRQ0 registration failed\n");
        return false;
    }
    console::write("[IRQ] PIT timer enabled on IRQ0\n");
    return true;
}

std::uint64_t pit_ticks() { return ticks; }
unsigned pit_frequency() { return configured_frequency; }

} // namespace shirley::platform::pc
