#pragma once

#include "shirley/boot_info.hpp"

// UEFI 韌體回報的記憶體地圖。OVMF（x86_64）與 EDK2/AAVMF（ARM64）用的是同一套
// 定義，因此和 E820、device tree 一樣放在共用的韌體格式層。
//
// The memory map reported by UEFI firmware. OVMF on x86_64 and EDK2/AAVMF on
// ARM64 use the same definitions, so this sits in the shared firmware format
// layer alongside E820 and the device tree.
namespace shirley::platform::firmware {

// EFI_MEMORY_TYPE 中我們會遇到的值。
// The EFI_MEMORY_TYPE values this code encounters.
enum EfiMemoryType : std::uint32_t {
    efi_reserved = 0,
    efi_loader_code = 1,
    efi_loader_data = 2,
    efi_boot_services_code = 3,
    efi_boot_services_data = 4,
    efi_runtime_services_code = 5,
    efi_runtime_services_data = 6,
    efi_conventional_memory = 7,
    efi_unusable_memory = 8,
    efi_acpi_reclaim_memory = 9,
    efi_acpi_memory_nvs = 10,
    efi_memory_mapped_io = 11,
    efi_memory_mapped_io_port_space = 12,
    efi_pal_code = 13,
    efi_persistent_memory = 14,
};

// EFI_MEMORY_DESCRIPTOR。韌體可能使用比這個結構更大的描述元，因此走訪陣列時
// 必須用韌體回報的 descriptor_size 當步幅，不能用 sizeof。
//
// EFI_MEMORY_DESCRIPTOR. Firmware may use a descriptor larger than this
// struct, so the array must be walked with the firmware-reported
// descriptor_size as the stride, never with sizeof.
struct EfiMemoryDescriptor {
    std::uint32_t type;
    std::uint32_t padding;
    std::uint64_t physical_start;
    std::uint64_t virtual_start;
    std::uint64_t page_count;
    std::uint64_t attribute;
};

// UEFI 的分頁固定為 4 KiB，與核心的 page_size 無關。
// A UEFI page is always 4 KiB, independent of the kernel's page_size.
constexpr std::uint64_t efi_page_size = 4096;

// 把 EFI 記憶體型別對應到通用型別。
// Map an EFI memory type onto the neutral type.
MemoryType uefi_classify(std::uint32_t efi_type);

// 走訪 UEFI 記憶體地圖並轉換成通用記憶體區段，回傳寫入的區段數。
// map_size 是整份地圖的位元組數，descriptor_size 是單一描述元的步幅。
// 呼叫 ExitBootServices 之後就不能再配置記憶體，因此 regions 必須由呼叫端事先備妥。
//
// Walk the UEFI memory map and convert it into neutral memory regions,
// returning how many were written. map_size is the byte size of the whole map
// and descriptor_size is the stride of one descriptor. No allocation is
// possible after ExitBootServices, so regions must be supplied by the caller.
std::uint64_t uefi_memory_map(const void* descriptors, std::uint64_t map_size,
                              std::uint64_t descriptor_size, BootMemoryRegion* regions,
                              std::uint64_t capacity);

} // namespace shirley::platform::firmware
