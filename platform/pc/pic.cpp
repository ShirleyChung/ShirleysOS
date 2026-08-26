#include "shirley/platform/pc/pic.hpp"

#include "shirley/arch/x86_64/port_io.hpp"

namespace shirley::platform::pc {
namespace {

using arch::x86_64::inb;
using arch::x86_64::io_wait;
using arch::x86_64::outb;

constexpr std::uint16_t master_command = 0x20;
constexpr std::uint16_t master_data = 0x21;
constexpr std::uint16_t slave_command = 0xa0;
constexpr std::uint16_t slave_data = 0xa1;
// ICW1：開始初始化並預期 ICW4。
// ICW1: begin initialization and expect an ICW4.
constexpr std::uint8_t initialize_command = 0x11;
// ICW4：8086/88 模式。
// ICW4: 8086/88 mode.
constexpr std::uint8_t mode_8086 = 0x01;
constexpr std::uint8_t end_of_interrupt_command = 0x20;
constexpr unsigned slave_irq = 2;

} // namespace

void pic_initialize() {
    outb(master_command, initialize_command);
    io_wait();
    outb(slave_command, initialize_command);
    io_wait();
    // ICW2：主控制器搬到 base_vector，從控制器接在其後 8 個向量。
    // ICW2: move the master to base_vector and the slave to the eight vectors
    // that follow.
    outb(master_data, static_cast<std::uint8_t>(base_vector));
    io_wait();
    outb(slave_data, static_cast<std::uint8_t>(base_vector + 8));
    io_wait();
    // ICW3：告知兩顆控制器串接在 IRQ2。
    // ICW3: tell both controllers they are cascaded on IRQ2.
    outb(master_data, 1u << slave_irq);
    io_wait();
    outb(slave_data, slave_irq);
    io_wait();
    outb(master_data, mode_8086);
    io_wait();
    outb(slave_data, mode_8086);
    io_wait();
    // 開機階段先遮罩全部 IRQ，由驅動程式自行解除。
    // Start with every IRQ masked and let drivers unmask what they own.
    outb(master_data, 0xff);
    outb(slave_data, 0xff);
}

void pic_mask(unsigned irq) {
    if (irq >= irq_count) return;
    const auto port = irq < 8 ? master_data : slave_data;
    const auto bit = static_cast<std::uint8_t>(1u << (irq & 7));
    outb(port, static_cast<std::uint8_t>(inb(port) | bit));
}

void pic_unmask(unsigned irq) {
    if (irq >= irq_count) return;
    const auto port = irq < 8 ? master_data : slave_data;
    const auto bit = static_cast<std::uint8_t>(1u << (irq & 7));
    outb(port, static_cast<std::uint8_t>(inb(port) & ~bit));
    // 從控制器的中斷必須經由 IRQ2 才會送到主控制器。
    // A slave interrupt only reaches the master through IRQ2.
    if (irq >= 8) pic_unmask(slave_irq);
}

void pic_end_of_interrupt(unsigned irq) {
    if (irq >= irq_count) return;
    // 從控制器的中斷要先通知從控制器，再通知主控制器。
    // A slave interrupt is acknowledged on the slave first, then the master.
    if (irq >= 8) outb(slave_command, end_of_interrupt_command);
    outb(master_command, end_of_interrupt_command);
}

} // namespace shirley::platform::pc
