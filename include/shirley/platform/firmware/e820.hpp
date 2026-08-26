#pragma once

#include "shirley/boot_info.hpp"

// 韌體資料格式；同一種格式可以被多個機器平台共用。
// E820 是 PC BIOS 回報實體記憶體地圖的方式。
//
// Firmware data formats; one format can be shared by several machines.
// E820 is how a PC BIOS reports its physical memory map.
namespace shirley::platform::firmware {

// BIOS INT 15h、EAX=E820 回報的單一項目。
// A single entry as reported by BIOS INT 15h with EAX=E820.
struct E820Entry {
    std::uint64_t base;
    std::uint64_t length;
    std::uint32_t type;
    std::uint32_t attributes;
};

// 開機載入器寫入固定位址的表格；entries 緊接在表頭之後。
// The table the boot loader writes at a fixed address; entries follow the
// header directly.
struct E820Table {
    std::uint32_t entry_count;
    std::uint32_t reserved;
    E820Entry entries[];
};

// E820 型別編號：1 可用、2 保留、3 ACPI 可回收、4 ACPI NVS、5 損壞。
// E820 type numbers: 1 usable, 2 reserved, 3 ACPI reclaimable, 4 ACPI NVS,
// 5 bad memory.
MemoryType classify(std::uint32_t e820_type);

// 將 E820 項目轉換成通用記憶體區段，回傳實際寫入的區段數。
// ACPI 3.0 的擴充屬性若標示項目無效則會被略過。
//
// Convert E820 entries into neutral memory regions and return how many were
// written. Entries the ACPI 3.0 extended attribute marks invalid are skipped.
std::uint64_t convert(const E820Entry* entries, std::uint64_t entry_count,
                      BootMemoryRegion* regions, std::uint64_t capacity);

// 追加一個區段，例如核心映像與韌體低位記憶體；空間不足時回傳 false。
// Append a region, such as the kernel image or low firmware memory; returns
// false when there is no room left.
bool append(BootMemoryRegion* regions, std::uint64_t& count, std::uint64_t capacity,
            std::uint64_t start, std::uint64_t length, MemoryType type);

} // namespace shirley::platform::firmware
