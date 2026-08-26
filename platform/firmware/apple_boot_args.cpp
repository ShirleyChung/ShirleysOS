#include "shirley/platform/firmware/apple_boot_args.hpp"

namespace shirley::platform::firmware {
namespace {

const AppleBootArgs* view(const void* arguments) {
    return static_cast<const AppleBootArgs*>(arguments);
}

bool append(BootMemoryRegion* regions, std::uint64_t& count, std::uint64_t capacity,
            std::uint64_t start, std::uint64_t length, MemoryType type) {
    if (length == 0 || count >= capacity) return false;
    regions[count++] = {start, length, type};
    return true;
}

} // namespace

bool apple_boot_args_valid(const void* arguments) {
    if (arguments == nullptr) return false;
    const auto* boot = view(arguments);
    if (boot->revision < apple_boot_args_minimum_revision) return false;
    // iBoot 一定會提供非零的實體基底位址與記憶體大小。
    // iBoot always supplies a non-zero physical base and memory size.
    if (boot->physical_base == 0 || boot->memory_size == 0) return false;
    // top_of_kernel_data 必須落在 RAM 內，否則結構不是我們認得的版本。
    // top_of_kernel_data must land inside RAM, otherwise this is not a layout
    // we recognise.
    if (boot->top_of_kernel_data < boot->physical_base) return false;
    if (boot->top_of_kernel_data - boot->physical_base > boot->memory_size) return false;
    return true;
}

std::uint64_t apple_boot_args_memory_map(const void* arguments, BootMemoryRegion* regions,
                                         std::uint64_t capacity) {
    if (regions == nullptr || capacity == 0 || !apple_boot_args_valid(arguments)) return 0;
    const auto* boot = view(arguments);
    std::uint64_t count = 0;
    // memory_size_actual 在較新的韌體上才有效，為 0 時退回 memory_size。
    // memory_size_actual is only meaningful on newer firmware; fall back to
    // memory_size when it is zero.
    const auto size = boot->memory_size_actual != 0 ? boot->memory_size_actual : boot->memory_size;
    append(regions, count, capacity, boot->physical_base, size, MemoryType::Usable);
    // 從 RAM 起點到 top_of_kernel_data 之間是韌體與已載入映像佔用的範圍。
    // Everything from the start of RAM up to top_of_kernel_data belongs to
    // firmware and the images it already loaded.
    append(regions, count, capacity, boot->physical_base,
           boot->top_of_kernel_data - boot->physical_base, MemoryType::Reserved);
    // Framebuffer 由顯示控制器持續存取，不能交給頁面分配器。
    // The display controller reads the framebuffer continuously, so it must
    // never reach the page allocator.
    if (boot->video.base_address != 0 && boot->video.stride != 0 && boot->video.height != 0) {
        append(regions, count, capacity, boot->video.base_address,
               boot->video.stride * boot->video.height, MemoryType::Reserved);
    }
    if (boot->device_tree != 0 && boot->device_tree_size != 0) {
        append(regions, count, capacity, boot->device_tree, boot->device_tree_size,
               MemoryType::Firmware);
    }
    return count;
}

bool apple_boot_args_framebuffer(const void* arguments, FramebufferInfo& framebuffer) {
    if (!apple_boot_args_valid(arguments)) return false;
    const auto& video = view(arguments)->video;
    if (video.base_address == 0 || video.width == 0 || video.height == 0) return false;
    framebuffer.address = video.base_address;
    framebuffer.width = static_cast<std::uint32_t>(video.width);
    framebuffer.height = static_cast<std::uint32_t>(video.height);
    framebuffer.pitch = static_cast<std::uint32_t>(video.stride);
    // depth 的低 16 位元是每像素位元數，其餘位元描述像素格式。
    // The low 16 bits of depth are bits per pixel; the rest describe the pixel
    // format.
    framebuffer.format = static_cast<std::uint32_t>(video.depth & 0xffff);
    return true;
}

} // namespace shirley::platform::firmware
