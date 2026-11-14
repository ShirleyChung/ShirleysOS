#pragma once

#include <cstddef>
#include <cstdint>

namespace shirley::rust {

// 初始化 Rust 子系統與驅動程式。
// Initialize the Rust subsystem and high-level Rust drivers.
bool initialize();

} // namespace shirley::rust

extern "C" {

struct ShirleyDeviceOps {
    int (*open)(void* driver_data);
    int (*close)(void* driver_data);
    long long (*read)(void* driver_data, void* buffer, std::size_t length);
    long long (*write)(void* driver_data, const void* buffer, std::size_t length);
};

// 給 Rust 呼叫的 ShirleyOS 核心服務
void* shirley_heap_allocate(std::size_t bytes, std::size_t alignment);
void shirley_heap_deallocate(void* ptr);
void shirley_console_write(const char* str, std::size_t len);

// Rust 驅動註冊字元裝置
int shirley_register_char_device(const char* name, const ShirleyDeviceOps* ops,
                                 void* driver_data);

// Rust 導出的初始化入口
int shirley_rust_init();

} // extern "C"
