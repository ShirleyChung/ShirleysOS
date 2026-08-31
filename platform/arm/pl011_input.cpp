#include "shirley/platform/arm/pl011.hpp"

#include "shirley/console.hpp"
#include "shirley/device.hpp"
#include "shirley/input_queue.hpp"
#include "shirley/io.hpp"
#include "shirley/irq.hpp"

namespace shirley::platform::arm {
namespace {

// PL011 的暫存器以 32 位元字為單位編號，與主控台後端使用的編號一致。
// PL011 registers are indexed as 32-bit words, the same numbering the console
// backend uses.
constexpr unsigned data = 0;
constexpr unsigned flag = 6;
// FR 的位元 5 表示傳送 FIFO 已滿；uart0 的寫入需要它。
// Bit 5 of the flag register means the transmit FIFO is full, which a write to
// uart0 needs.
constexpr unsigned transmit_full = 1u << 5;
constexpr unsigned interrupt_fifo_level = 13;
constexpr unsigned interrupt_mask = 14;
constexpr unsigned interrupt_clear = 17;
// FR 的位元 4 表示接收 FIFO 已空。
// Bit 4 of the flag register means the receive FIFO is empty.
constexpr unsigned receive_empty = 1u << 4;
// IMSC 的位元 4 是「收到資料」，位元 6 是「接收逾時」。只開前者的話，不足
// 觸發門檻的零星按鍵會一直留在 FIFO 裡等不到中斷，而終端機正是一次送一個
// 字元，因此兩個都必須開。
//
// IMSC bit 4 is "data received" and bit 6 is "receive timeout". With only the
// former, a handful of keys below the FIFO trigger level would sit there
// waiting for an interrupt that never comes — and a terminal sends exactly one
// character at a time — so both are needed.
constexpr unsigned receive_interrupt = 1u << 4;
constexpr unsigned receive_timeout_interrupt = 1u << 6;
// IFLS：接收 FIFO 到 1/8 就送出中斷，也就是收到第一個位元組就送。
// IFLS: raise the interrupt once the receive FIFO is one eighth full, which
// means as soon as the first byte lands.
constexpr unsigned receive_level_eighth = 0u << 3;
// 寫入 1 即清除對應的中斷狀態。
// Writing a one clears the matching interrupt status.
constexpr unsigned clear_all = 0x7ff;

volatile unsigned int* uart = nullptr;
std::uint64_t characters = 0;

// UART 自己的環狀緩衝區，也就是 uart0 的接收內容。
// The UART's own ring buffer, which is what uart0 receives into.
io::InputQueue receive_queue;

io::Result uart_read(device::Device&, void* buffer, std::size_t length) {
    return receive_queue.read(buffer, length);
}

// uart0 的傳送。平台的主控台後端寫的是同一組暫存器，這裡不共用它是因為
// 後端屬於各平台（QEMU virt 與其他 PL011 機器各有一份），而這個驅動程式是
// 共用的；兩邊都只是「FIFO 有空位就放一個位元組」，重複的是那一件事本身。
//
// uart0's transmit side. The platform console backend writes the same
// registers; it is not shared here because a backend belongs to its platform —
// QEMU virt and other PL011 machines each have their own — while this driver
// is common to all of them. Both amount to "put a byte in when the FIFO has
// room", and that is the whole of the duplication.
io::Result uart_write(device::Device&, const void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (uart == nullptr) return {0, io::Error::Unsupported};
    const auto* text = static_cast<const char*>(buffer);
    for (std::size_t i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            while (uart[flag] & transmit_full) {}
            uart[data] = '\r';
        }
        while (uart[flag] & transmit_full) {}
        uart[data] = static_cast<unsigned char>(text[i]);
    }
    return {length, io::Error::None};
}

constexpr device::Operations uart_operations{nullptr, nullptr, uart_read, uart_write, nullptr};

constinit device::Device uart_device{"uart0", device::Type::Character, uart_operations};

// 終端機的 Enter 是回車符、Backspace 通常是 DEL，行編輯器只認得 '\n' 與
// '\b'，因此在這裡就換過來。
//
// A terminal sends carriage return for Enter and usually DEL for Backspace,
// while the line editor knows only '\n' and '\b', so they are translated here.
char normalize(unsigned int value) {
    const auto character = static_cast<char>(value & 0xff);
    if (character == '\r') return '\n';
    if (character == 0x7f) return '\b';
    return character;
}

// 讀到 FIFO 空為止：一次中斷可能對應好幾個位元組，剩下的位元組會讓中斷
// 狀態一直維持在待處理。迴圈有次數上限，壞掉的 UART 不可以讓核心停在這裡。
//
// Read until the FIFO is empty: one interrupt can cover several bytes, and a
// leftover byte would keep the interrupt pending forever. The loop is bounded
// so a broken UART cannot park the kernel here.
constexpr unsigned bytes_per_interrupt = 32;

void uart_interrupt(unsigned, void*) {
    if (uart == nullptr) return;
    for (unsigned byte = 0; byte < bytes_per_interrupt; ++byte) {
        if ((uart[flag] & receive_empty) != 0) break;
        ++characters;
        // 中斷處理常式只把字元放進緩衝區；滿了就丟掉，不阻塞。
        // The handler only puts the character in the buffer; a full buffer
        // drops it rather than blocking.
        (void)receive_queue.push(normalize(uart[data]));
    }
    uart[interrupt_clear] = clear_all;
}

} // namespace

bool pl011_input_initialize(std::uintptr_t base, unsigned irq) {
    if (base == 0) return false;
    uart = reinterpret_cast<volatile unsigned int*>(base);
    characters = 0;
    receive_queue.clear();
    uart[interrupt_fifo_level] = receive_level_eighth;
    uart[interrupt_clear] = clear_all;
    uart[interrupt_mask] = receive_interrupt | receive_timeout_interrupt;

    if (!irq::request(irq, uart_interrupt)) {
        console::write("[IRQ] UART receive IRQ registration failed\n");
        uart = nullptr;
        return false;
    }
    if (device::register_device(uart_device) != device::Status::Ok) {
        console::write("[device] uart0 registration failed\n");
        irq::release(irq);
        uart = nullptr;
        return false;
    }
    console::attach_input(uart_device);
    console::write("[IRQ] UART console input enabled\n");
    return true;
}

device::Device* pl011_device() { return &uart_device; }
std::uint64_t pl011_input_characters() { return characters; }

} // namespace shirley::platform::arm
