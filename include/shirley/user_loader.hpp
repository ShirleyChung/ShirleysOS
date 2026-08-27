#pragma once

#include "shirley/address_space.hpp"
#include "shirley/boot/elf64.hpp"

#include <cstddef>
#include <cstdint>

namespace shirley::user {

const void* embedded_image();
std::size_t embedded_image_size();

struct Image {
    std::uint64_t entry = 0;
    std::uint64_t stack = 0;
    std::uint64_t stack_bottom = 0;
    std::size_t segment_count = 0;
};

// 載入靜態 user ELF：配置並映射 PT_LOAD 頁面、複製檔案內容、清除 .bss，
// 並配置一頁可寫入的初始 user stack。
// Load a static user ELF: allocate and map PT_LOAD pages, copy file contents,
// clear .bss, and allocate one writable initial user-stack page.
bool load_elf(const void* image, std::size_t size, std::uint16_t machine,
              memory::AddressSpace& address_space, Image& result);

// 啟動建置時嵌入的 hello user image；成功後不會返回。
// Start the hello user image embedded at build time; does not return on success.
bool launch_embedded();

} // namespace shirley::user
