#include "shirley/rust_bridge.hpp"
#include "shirley/console.hpp"
#include "shirley/device.hpp"
#include "shirley/heap.hpp"
#include "shirley/io.hpp"

namespace {

constexpr std::size_t max_rust_devices = 4;

struct RustDeviceContext {
    shirley::device::Device device{};
    shirley::device::Operations ops{};
    const ShirleyDeviceOps* rust_ops = nullptr;
    void* rust_driver_data = nullptr;
    bool in_use = false;
};

RustDeviceContext rust_devices[max_rust_devices];
std::size_t rust_device_count = 0;

int rust_dev_open_wrapper(shirley::device::Device& dev) {
    auto* ctx = static_cast<RustDeviceContext*>(dev.driver_data);
    if (ctx != nullptr && ctx->rust_ops != nullptr && ctx->rust_ops->open != nullptr) {
        return ctx->rust_ops->open(ctx->rust_driver_data);
    }
    return 0;
}

int rust_dev_close_wrapper(shirley::device::Device& dev) {
    auto* ctx = static_cast<RustDeviceContext*>(dev.driver_data);
    if (ctx != nullptr && ctx->rust_ops != nullptr && ctx->rust_ops->close != nullptr) {
        return ctx->rust_ops->close(ctx->rust_driver_data);
    }
    return 0;
}

shirley::io::Result rust_dev_read_wrapper(shirley::device::Device& dev, void* buffer,
                                          std::size_t length) {
    auto* ctx = static_cast<RustDeviceContext*>(dev.driver_data);
    if (ctx == nullptr || ctx->rust_ops == nullptr || ctx->rust_ops->read == nullptr) {
        return {0, shirley::io::Error::Unsupported};
    }
    long long res = ctx->rust_ops->read(ctx->rust_driver_data, static_cast<unsigned char*>(buffer), length);
    if (res < 0) {
        return {0, shirley::io::Error::DeviceError};
    }
    return {static_cast<std::size_t>(res), shirley::io::Error::None};
}

shirley::io::Result rust_dev_write_wrapper(shirley::device::Device& dev,
                                           const void* buffer, std::size_t length) {
    auto* ctx = static_cast<RustDeviceContext*>(dev.driver_data);
    if (ctx == nullptr || ctx->rust_ops == nullptr || ctx->rust_ops->write == nullptr) {
        return {0, shirley::io::Error::Unsupported};
    }
    long long res = ctx->rust_ops->write(ctx->rust_driver_data, static_cast<const unsigned char*>(buffer), length);
    if (res < 0) {
        return {0, shirley::io::Error::DeviceError};
    }
    return {static_cast<std::size_t>(res), shirley::io::Error::None};
}

} // namespace

extern "C" {

void* shirley_heap_allocate(std::size_t bytes, std::size_t alignment) {
    return shirley::heap::allocate(bytes, alignment);
}

void shirley_heap_deallocate(void* ptr) {
    shirley::heap::free(ptr);
}

void shirley_console_write(const char* str, std::size_t len) {
    if (str == nullptr || len == 0) return;
    for (std::size_t i = 0; i < len; ++i) {
        char ch[2] = {str[i], '\0'};
        shirley::console::write(ch);
    }
}

int shirley_register_char_device(const char* name, const ShirleyDeviceOps* ops,
                                 void* driver_data) {
    if (name == nullptr || ops == nullptr || rust_device_count >= max_rust_devices) {
        return -1;
    }

    auto& ctx = rust_devices[rust_device_count];
    ctx.rust_ops = ops;
    ctx.rust_driver_data = driver_data;
    ctx.in_use = true;

    ctx.ops.open = rust_dev_open_wrapper;
    ctx.ops.close = rust_dev_close_wrapper;
    ctx.ops.read = rust_dev_read_wrapper;
    ctx.ops.write = rust_dev_write_wrapper;

    ctx.device = shirley::device::Device(name, shirley::device::Type::Character, ctx.ops, &ctx);

    const auto status = shirley::device::register_device(ctx.device);
    if (status != shirley::device::Status::Ok) {
        ctx.in_use = false;
        return -1;
    }

    ++rust_device_count;
    return 0;
}

} // extern "C"

namespace shirley::rust {

bool initialize() {
    return shirley_rust_init() == 0;
}

} // namespace shirley::rust
