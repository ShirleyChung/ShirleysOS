#include "shirley/console.hpp"

#include "shirley/device.hpp"
#include "shirley/io.hpp"

namespace shirley::console {
namespace {

// 已接上的輸入來源，依接上的順序排列。表格很小而且只在開機時被寫入，因此
// 用線性掃描就夠了。
//
// The attached input sources, in the order they were attached. The table is
// small and only written during boot, so a linear scan is all it needs.
device::Device* inputs[max_input_devices];
std::size_t input_devices = 0;

// 主控台輸入的串流形式。主控台不是一個裝置的別名，而是若干裝置的匯流點，
// 因此它自己就是串流，不能只是把某個裝置包起來。
//
// The console's input as a stream. The console is not an alias for one device
// but the point where several meet, so it is a stream in its own right rather
// than a wrapper around a single device.
class InputStream final : public io::ByteStream {
public:
    io::Result read(void* buffer, std::size_t length) override {
        return console::read(buffer, length);
    }
    // 輸入串流只用於讀取；要輸出的人應該走 console::write()。
    // The input stream is read-only; anything writing should go through
    // console::write().
    io::Result write(const void*, std::size_t) override { return {0, io::Error::Unsupported}; }
};

InputStream input_stream_object;

// 主控台裝置：讀寫都轉回主控台層，因此 /dev/console 和直接呼叫 console 的
// 效果完全一樣。
//
// The console device: both directions go back through the console layer, so
// /dev/console and a direct console call behave identically.
io::Result console_read(device::Device&, void* buffer, std::size_t length) {
    return console::read(buffer, length);
}

io::Result console_write(device::Device&, const void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    console::write(static_cast<const char*>(buffer), length);
    return {length, io::Error::None};
}

constexpr device::Operations console_operations{nullptr, nullptr, console_read, console_write,
                                                nullptr};

constinit device::Device console_device_object{"console", device::Type::Character,
                                               console_operations};

} // namespace

device::Device& console_device() { return console_device_object; }
io::ByteStream& input_stream() { return input_stream_object; }

bool attach_input(device::Device& device) {
    if (input_devices >= max_input_devices) return false;
    for (std::size_t i = 0; i < input_devices; ++i) {
        if (inputs[i] == &device) return true;
    }
    inputs[input_devices++] = &device;
    // 有了第一個輸入裝置，標準輸入才有東西可指；在那之前它必須維持未設定，
    // 因為 shell 是以標準輸入是否存在來判斷這台機器能不能打字。
    //
    // Standard input has something to point at only once the first input
    // device exists. Until then it stays unset, because the shell decides
    // whether this machine can be typed at by whether standard input exists.
    if (input_devices == 1) io::set_standard_input(&input_stream_object);
    return true;
}

bool detach_input(device::Device& device) {
    for (std::size_t i = 0; i < input_devices; ++i) {
        if (inputs[i] != &device) continue;
        // 最後一個補上這個位置，表格因此保持連續。
        // The last entry moves into this slot, which keeps the table dense.
        inputs[i] = inputs[input_devices - 1];
        inputs[--input_devices] = nullptr;
        if (input_devices == 0) io::set_standard_input(nullptr);
        return true;
    }
    return false;
}

std::size_t input_count() { return input_devices; }

device::Device* input_device(std::size_t index) {
    return index < input_devices ? inputs[index] : nullptr;
}

io::Result read(void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (length == 0) return {0, io::Error::None};
    for (std::size_t i = 0; i < input_devices; ++i) {
        const auto result = inputs[i]->read(buffer, length);
        // 一個空的來源不是錯誤，只是這一輪沒有東西；換下一個問。真正的錯誤
        // 直接回報，不要讓壞掉的裝置看起來只是很安靜。
        //
        // An empty source is not an error, only nothing this time around, so
        // the next one is asked. A real error is reported as such: a broken
        // device must not merely look quiet.
        if (!result) return result;
        if (result.transferred != 0) return result;
    }
    return {0, io::Error::None};
}

} // namespace shirley::console
