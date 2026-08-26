#include "shirley/platform/firmware/uefi.hpp"

namespace shirley::platform::firmware {

MemoryType uefi_classify(std::uint32_t efi_type) {
    switch (efi_type) {
        // 一般可用記憶體，以及載入器與 boot services 用完即可回收的區段。
        // Ordinary free memory, plus the loader and boot services ranges that
        // become reusable once ExitBootServices has been called.
        case efi_conventional_memory:
        case efi_boot_services_code:
        case efi_boot_services_data:
            return MemoryType::Usable;
        // 載入器自己的程式碼與資料仍在使用中，交接完成前不能回收。
        // The loader's own code and data are still live and must not be
        // reclaimed while the handoff is still in use.
        case efi_loader_code:
        case efi_loader_data:
            return MemoryType::Reserved;
        // ACPI 表格在核心讀完之後才能回收。
        // ACPI tables become reusable only after the kernel has read them.
        case efi_acpi_reclaim_memory:
            return MemoryType::Reclaimable;
        case efi_runtime_services_code:
        case efi_runtime_services_data:
        case efi_acpi_memory_nvs:
        case efi_pal_code:
            return MemoryType::Firmware;
        case efi_memory_mapped_io:
        case efi_memory_mapped_io_port_space:
            return MemoryType::Mmio;
        // 未知型別一律視為不可使用，這是唯一安全的預設值。
        // An unknown type is treated as unusable, the only safe default.
        default:
            return MemoryType::Reserved;
    }
}

std::uint64_t uefi_memory_map(const void* descriptors, std::uint64_t map_size,
                              std::uint64_t descriptor_size, BootMemoryRegion* regions,
                              std::uint64_t capacity) {
    if (descriptors == nullptr || regions == nullptr) return 0;
    // 描述元至少要放得下我們讀取的欄位，步幅為零則無法走訪。
    // A descriptor must be large enough for the fields read here, and a zero
    // stride cannot be walked at all.
    if (descriptor_size < sizeof(EfiMemoryDescriptor)) return 0;

    const auto* bytes = static_cast<const unsigned char*>(descriptors);
    std::uint64_t count = 0;
    for (std::uint64_t offset = 0; offset + descriptor_size <= map_size && count < capacity;
         offset += descriptor_size) {
        const auto* entry = reinterpret_cast<const EfiMemoryDescriptor*>(bytes + offset);
        if (entry->page_count == 0) continue;
        // 長度以 4 KiB 分頁計算，並檢查是否溢位。
        // The length is a count of 4 KiB pages; check it cannot overflow.
        if (entry->page_count > (~std::uint64_t{0}) / efi_page_size) continue;
        const auto length = entry->page_count * efi_page_size;
        if (entry->physical_start > (~std::uint64_t{0}) - length) continue;
        regions[count++] = {entry->physical_start, length, uefi_classify(entry->type)};
    }
    return count;
}

} // namespace shirley::platform::firmware
