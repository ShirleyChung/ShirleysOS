#include "shirley/platform/pc/ps2_keyboard.hpp"

#include "shirley/arch/x86_64/port_io.hpp"
#include "shirley/console.hpp"
#include "shirley/format.hpp"
#include "shirley/input/scancode.hpp"
#include "shirley/input_queue.hpp"
#include "shirley/io.hpp"
#include "shirley/irq.hpp"

namespace shirley::platform::pc {
namespace {

using arch::x86_64::inb;
using arch::x86_64::io_wait;
using arch::x86_64::outb;

// 8042 控制器：資料埠讀寫按鍵位元組，命令／狀態埠控制控制器本身。
// The 8042 controller: the data port carries key bytes, and the command and
// status port drives the controller itself.
constexpr std::uint16_t data_port = 0x60;
constexpr std::uint16_t command_port = 0x64;
// 狀態位元 0：有資料可讀。狀態位元 1：控制器尚未取走上一個寫入。
// Status bit 0: a byte is waiting to be read. Status bit 1: the controller has
// not consumed the previous write yet.
constexpr std::uint8_t status_output_full = 1u << 0;
constexpr std::uint8_t status_input_full = 1u << 1;
// 狀態位元 5：等在輸出緩衝區的是第二個連接埠（滑鼠）送來的位元組。兩個
// 連接埠共用同一個資料埠，因此不檢查這個位元就會把滑鼠封包當成掃描碼解讀。
//
// Status bit 5: the byte waiting in the output buffer came from the second
// port, the mouse. Both ports share one data port, so without this check a
// mouse packet would be decoded as scancodes.
constexpr std::uint8_t status_auxiliary = 1u << 5;
// 讀寫控制器設定位元組的命令。
// The commands that read and write the controller configuration byte.
constexpr std::uint8_t command_read_configuration = 0x20;
constexpr std::uint8_t command_write_configuration = 0x60;
// 設定位元組位元 0：第一個連接埠產生 IRQ1。位元 4：停用第一個連接埠時脈。
// Configuration bit 0: the first port raises IRQ1. Bit 4: the first port's
// clock is disabled.
constexpr std::uint8_t configuration_first_port_interrupt = 1u << 0;
constexpr std::uint8_t configuration_first_port_clock_disabled = 1u << 4;

// 控制器可能一直沒有回應，因此每個等待迴圈都要有次數上限，
// 不能讓開機流程卡死在這裡。
//
// The controller may never answer, so every wait loop is bounded. Boot must
// not be able to hang here.
constexpr unsigned wait_attempts = 100000;

input::ScancodeDecoder decoder;
io::InputQueue queue;
std::uint64_t characters = 0;

bool wait_writable() {
    for (unsigned attempt = 0; attempt < wait_attempts; ++attempt) {
        if ((inb(command_port) & status_input_full) == 0) return true;
        io_wait();
    }
    return false;
}

bool wait_readable() {
    for (unsigned attempt = 0; attempt < wait_attempts; ++attempt) {
        if ((inb(command_port) & status_output_full) != 0) return true;
        io_wait();
    }
    return false;
}

// 丟掉韌體留在輸出緩衝區裡的位元組。若不清空，第一個 IRQ1 之前就已經有資料
// 卡在那裡，控制器便不會再產生新的中斷。
//
// Discard whatever the firmware left in the output buffer. A byte still
// sitting there before the first IRQ1 stops the controller from raising any
// further interrupt.
void drain_output() {
    for (unsigned attempt = 0; attempt < wait_attempts; ++attempt) {
        if ((inb(command_port) & status_output_full) == 0) return;
        (void)inb(data_port);
    }
}

// 確保控制器真的會為第一個連接埠送出 IRQ1。韌體交出控制權時的設定不保證
// 是我們要的，因此明確寫回一次。
//
// Make sure the controller really raises IRQ1 for the first port. Whatever the
// firmware left behind is not guaranteed to be what is wanted, so the setting
// is written back explicitly.
bool enable_first_port_interrupt() {
    if (!wait_writable()) return false;
    outb(command_port, command_read_configuration);
    if (!wait_readable()) return false;
    auto configuration = inb(data_port);
    configuration = static_cast<std::uint8_t>(configuration | configuration_first_port_interrupt);
    configuration = static_cast<std::uint8_t>(configuration & ~configuration_first_port_clock_disabled);
    if (!wait_writable()) return false;
    outb(command_port, command_write_configuration);
    if (!wait_writable()) return false;
    outb(data_port, configuration);
    return true;
}

// 要求鍵盤本身開始送出掃描碼。韌體通常已經開啟，但不能假設：這個命令寫的是
// 資料埠，收件者是鍵盤而不是控制器。回應的 ACK 之後會被 drain_output() 清掉。
//
// Ask the keyboard itself to start sending scancodes. Firmware usually left it
// enabled, but that cannot be assumed. This command goes to the data port and
// is addressed to the keyboard, not to the controller; drain_output() clears
// the ACK it replies with.
constexpr std::uint8_t keyboard_enable_scanning = 0xf4;

void enable_scanning() {
    if (!wait_writable()) return;
    outb(data_port, keyboard_enable_scanning);
}

// 把字元回顯到主控台。Backspace 要真的把畫面上的字元擦掉，因此送出
// 「退一格、寫空白、再退一格」。
//
// Echo a character to the console. A backspace has to actually erase the
// character on screen, so it is sent as back up, overwrite with a space, back
// up again.
void echo(char value) {
    if (value == '\b') {
        console::write("\b \b");
        return;
    }
    const char text[2] = {value, '\0'};
    console::write(text, 1);
}

#ifdef SHIRLEY_DEBUG_SCANCODES
// 建置時定義 SHIRLEY_DEBUG_SCANCODES 就會印出每一個原始掃描碼。
// Defining SHIRLEY_DEBUG_SCANCODES at build time prints every raw scancode.
void log_scancode(std::uint8_t code) {
    char digits[3];
    console::write("[IRQ] scancode 0x");
    format::to_hex(digits, sizeof(digits), code, 2);
    console::write(digits);
    console::write("\n");
}
#else
void log_scancode(std::uint8_t) {}
#endif

// IRQ1 處理常式。一次中斷可能對應多個位元組（例如擴充鍵的前綴），因此把
// 輸出緩衝區讀到空為止，否則殘留的位元組會擋住下一個中斷。迴圈仍然有次數
// 上限：中斷處理常式在關閉中斷的狀態下執行，壞掉的控制器不可以讓核心
// 永遠停在這裡。
//
// The IRQ1 handler. One interrupt can carry more than one byte — an extended
// key's prefix, for instance — so the output buffer is read until it is empty.
// A leftover byte would block the next interrupt. The loop is still bounded: a
// handler runs with interrupts disabled, and a broken controller must not be
// able to park the kernel here forever.
constexpr unsigned bytes_per_interrupt = 16;

void keyboard_interrupt(unsigned, void*) {
    for (unsigned byte = 0; byte < bytes_per_interrupt; ++byte) {
        const auto status = inb(command_port);
        if ((status & status_output_full) == 0) return;
        const auto code = inb(data_port);
        // 滑鼠的位元組必須讀走才不會擋住鍵盤，但絕對不能拿去解掃描碼。
        // A mouse byte has to be taken out of the way so it cannot block the
        // keyboard, but it must never be decoded as a scancode.
        if ((status & status_auxiliary) != 0) continue;
        log_scancode(code);
        const char decoded = decoder.feed(code);
        if (decoded == '\0') continue;
        ++characters;
        queue.push(decoded);
        echo(decoded);
    }
}

} // namespace

bool ps2_keyboard_initialize() {
    decoder.reset();
    queue.clear();
    characters = 0;

    if (!enable_first_port_interrupt()) {
        console::write("[IRQ] PS/2 controller did not respond; keyboard disabled\n");
        return false;
    }
    enable_scanning();
    // 最後才清空緩衝區：韌體留下的位元組與剛才那個 ACK 都必須被讀走，
    // 否則控制器會認為主機還沒收完資料，之後就不再產生中斷。
    //
    // Draining comes last: whatever the firmware left behind and the ACK just
    // received both have to be taken out, or the controller believes the host
    // has not finished reading and raises no further interrupt.
    drain_output();

    if (!irq::request(ps2_keyboard_irq, keyboard_interrupt)) {
        console::write("[IRQ] keyboard IRQ1 registration failed\n");
        return false;
    }
    // 有了輸入裝置之後標準輸入才有東西可接。
    // Standard input has something to point at only once an input device
    // exists.
    io::set_standard_input(&queue);
    console::write("[IRQ] keyboard IRQ enabled\n");
    return true;
}

std::uint64_t ps2_keyboard_pending() { return queue.available(); }
std::uint64_t ps2_keyboard_characters() { return characters; }

} // namespace shirley::platform::pc
