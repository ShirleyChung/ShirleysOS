#include "shirley/platform/pc/serial.hpp"

#include "shirley/arch/x86_64/port_io.hpp"
#include "shirley/console.hpp"
#include "shirley/io.hpp"
#include "shirley/irq.hpp"

namespace shirley::platform::pc {
namespace {

using arch::x86_64::inb;
using arch::x86_64::outb;

std::uint64_t characters = 0;

// 序列埠終端機送來的 Enter 是回車符，Backspace 通常是 DEL；主控台的行編輯器
// 只認得 '\n' 與 '\b'，所以在這裡就換成它認得的形式，讓鍵盤與序列埠送進
// 共用佇列的字元完全一樣。
//
// A serial terminal sends carriage return for Enter and usually DEL for
// Backspace. The console's line editor knows only '\n' and '\b', so the
// translation happens here, which keeps the characters a keyboard and a serial
// port put into the shared queue identical.
constexpr char carriage_return = '\r';
constexpr char delete_character = 0x7f;

char normalize(std::uint8_t value) {
    const auto character = static_cast<char>(value);
    if (character == carriage_return) return '\n';
    if (character == delete_character) return '\b';
    return character;
}

// IRQ4 處理常式。一次中斷可能對應好幾個位元組，因為 UART 的 FIFO 會累積到
// 觸發門檻才送出中斷，因此要讀到接收暫存器空為止；剩下的位元組會擋住下一個
// 中斷。迴圈有次數上限：處理常式在關閉中斷的狀態下執行，壞掉的 UART 不可以
// 讓核心永遠停在這裡。
//
// The IRQ4 handler. One interrupt can carry several bytes because the UART's
// FIFO fills to a trigger level before raising one, so the receive register is
// read until it is empty; a leftover byte would block the next interrupt. The
// loop is bounded: a handler runs with interrupts disabled, and a broken UART
// must not be able to park the kernel here forever.
constexpr unsigned bytes_per_interrupt = 32;

void serial_interrupt(unsigned, void*) {
    for (unsigned byte = 0; byte < bytes_per_interrupt; ++byte) {
        if ((inb(com1_line_status) & com1_data_ready) == 0) return;
        const auto value = inb(com1_data);
        ++characters;
        io::console_input_push(normalize(value));
    }
}

} // namespace

bool serial_input_initialize() {
    characters = 0;
    // OUT2 沒有接通時 UART 的中斷線根本到不了中斷控制器，因此先確認它是開的。
    // The UART's interrupt line never reaches the controller while OUT2 is
    // low, so it is asserted before anything else.
    outb(com1_modem_control, static_cast<std::uint8_t>(com1_modem_ready | com1_modem_out2));
    // 主控台後端把觸發門檻設在 14 個位元組，那對輸出沒有影響，但對輸入來說
    // 代表按不到 14 個鍵就不會有中斷；改成收到第一個位元組就送。
    //
    // The console backend leaves the trigger level at fourteen bytes, which
    // does not matter for output but means fewer than fourteen keystrokes
    // raise no interrupt at all. Lower it so the first byte interrupts.
    outb(com1_fifo_control, com1_fifo_enable_single_byte);
    outb(com1_interrupt_enable, com1_receive_interrupt);
    // 韌體留在接收暫存器裡的位元組會讓 UART 認為主機還沒讀完，之後就不再
    // 產生中斷，所以要先清空。
    //
    // A byte the firmware left in the receive register makes the UART believe
    // the host has not caught up, and it then raises no further interrupt, so
    // it is drained first.
    for (unsigned byte = 0; byte < bytes_per_interrupt; ++byte) {
        if ((inb(com1_line_status) & com1_data_ready) == 0) break;
        (void)inb(com1_data);
    }

    if (!irq::request(serial_irq, serial_interrupt)) {
        console::write("[IRQ] serial IRQ4 registration failed\n");
        return false;
    }
    io::attach_console_input();
    console::write("[IRQ] serial console input enabled on IRQ4\n");
    return true;
}

std::uint64_t serial_input_characters() { return characters; }

} // namespace shirley::platform::pc
