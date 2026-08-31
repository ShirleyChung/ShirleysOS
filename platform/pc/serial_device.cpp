#include "shirley/platform/pc/serial.hpp"

#include "shirley/console.hpp"
#include "shirley/device.hpp"
#include "shirley/io.hpp"

// COM1 的裝置包裝。這個檔案不碰任何 I/O port：傳送交給主控台後端那條路徑，
// 接收取自 IRQ4 已經填好的環狀緩衝區。裝置抽象只是把既有的兩端接起來並取一
// 個名字，硬體行為完全沒有改變。
//
// The device wrapper for COM1. This file touches no I/O port: transmitting
// goes through the console backend's path and receiving takes from the ring
// buffer IRQ4 has already filled. The abstraction joins two existing halves
// and gives them a name; no hardware behaviour changes.
namespace shirley::platform::pc {
namespace {

io::Result uart_read(device::Device&, void* buffer, std::size_t length) {
    // 佇列空的時候傳輸 0 個位元組，而不是回報錯誤：沒有人在打字不是故障。
    // An empty queue transfers zero bytes rather than reporting an error:
    // nobody typing is not a fault.
    return serial_receive_queue().read(buffer, length);
}

io::Result uart_write(device::Device&, const void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    serial_write(static_cast<const char*>(buffer), length);
    // 傳送端會等到保持暫存器清空才送下一個位元組，因此回到這裡時每個位元組
    // 都已經真的送出去了。
    //
    // The transmit path waits for the holding register to drain between bytes,
    // so by the time it returns every byte has genuinely gone out.
    return {length, io::Error::None};
}

constexpr device::Operations uart_operations{nullptr, nullptr, uart_read, uart_write, nullptr};

// uart0 是字元裝置而不是輸入裝置：它兩個方向都能用，而輸入只是其中一半。
// uart0 is a character device rather than an input one: it works in both
// directions, and input is only half of that.
constinit device::Device uart_device{"uart0", device::Type::Character, uart_operations};

} // namespace

bool serial_device_register() {
    if (device::register_device(uart_device) == device::Status::Ok) return true;
    console::write("[device] uart0 registration failed\n");
    return false;
}

device::Device* serial_device() { return &uart_device; }

} // namespace shirley::platform::pc
