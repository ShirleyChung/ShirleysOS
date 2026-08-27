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
// OCW3：把接下來的命令埠讀取切換成讀取服務中暫存器（ISR）。
// OCW3: make the next read of the command port return the in-service register.
constexpr std::uint8_t read_in_service_command = 0x0b;
constexpr unsigned slave_irq = cascade_irq;

// 讀取某顆控制器的 ISR。對應位元為 1 代表該中斷確實正在服務中。
// Read one controller's in-service register. A set bit means that interrupt is
// genuinely being serviced.
std::uint8_t in_service(std::uint16_t command_port) {
    outb(command_port, read_in_service_command);
    return inb(command_port);
}

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
    // 開機階段先遮罩全部裝置 IRQ，由驅動程式自行解除。串接線例外：它不是
    // 裝置中斷，遮住它會讓整顆從控制器的 IRQ8-15 都送不出來。
    //
    // Start with every device IRQ masked and let drivers unmask what they own.
    // The cascade line is the exception: it is not a device interrupt, and
    // masking it would silence the slave's whole IRQ8-15 range.
    outb(master_data, static_cast<std::uint8_t>(0xff & ~(1u << slave_irq)));
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

bool pic_spurious(unsigned irq) {
    // 只有每顆控制器最低優先權的那條線會被拿來回報假中斷。
    // Only the lowest-priority line on each controller is ever used to report
    // a spurious interrupt.
    if (irq == master_spurious_irq)
        return (in_service(master_command) & (1u << master_spurious_irq)) == 0;
    if (irq == slave_spurious_irq) {
        if ((in_service(slave_command) & (1u << (slave_spurious_irq & 7))) != 0) return false;
        // 主控制器並不知道從控制器送來的是假中斷，它那邊的 IRQ2 確實在服務
        // 中，因此仍然要收到 end-of-interrupt。
        //
        // The master cannot tell that the slave's interrupt was spurious. Its
        // own IRQ2 really is in service, so it still needs an
        // end-of-interrupt.
        outb(master_command, end_of_interrupt_command);
        return true;
    }
    return false;
}

} // namespace shirley::platform::pc
