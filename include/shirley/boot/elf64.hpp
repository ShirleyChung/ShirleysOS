#pragma once

#include <cstddef>
#include <cstdint>

// 開機載入器共用的 ELF64 讀取器。核心以 ELF 交給載入器，載入器必須自行走訪
// program header 才知道要把哪些內容搬到哪個實體位址。
// 這裡只做讀取與驗證，不碰記憶體配置，因此可以在主機測試中直接驗證。
//
// The ELF64 reader shared by boot loaders. The kernel is handed to a loader as
// an ELF, so the loader must walk the program headers itself to know what to
// copy where. This code only reads and validates; it never allocates, so it
// can be exercised directly by host tests.
namespace shirley::boot {

// e_machine 的值；載入器用它確認 ELF 與自己的架構相符。
// e_machine values, used by a loader to confirm the ELF matches its own
// architecture.
constexpr std::uint16_t elf_machine_x86_64 = 62;
constexpr std::uint16_t elf_machine_aarch64 = 183;

// 一個需要載入的 PT_LOAD 節區。
// One PT_LOAD segment that has to be loaded.
struct Elf64Segment {
    std::uint64_t file_offset;
    std::uint64_t file_size;
    std::uint64_t physical_address;
    std::uint64_t virtual_address;
    // memory_size 大於 file_size 的部分是 .bss，必須清零。
    // The part of memory_size beyond file_size is .bss and must be zeroed.
    std::uint64_t memory_size;
    bool readable;
    bool writable;
    bool executable;
};

// 確認 buffer 是可以載入的 ELF64 靜態執行檔，且 e_machine 與 machine 相符。
// Check that buffer holds a loadable static ELF64 executable whose e_machine
// matches machine.
bool elf64_valid(const void* buffer, std::size_t size, std::uint16_t machine);

// 取得進入點虛擬位址；ELF 不合法時回傳 0。
// The entry point virtual address, or 0 when the ELF is not valid.
std::uint64_t elf64_entry(const void* buffer, std::size_t size, std::uint16_t machine);

// 取出所有 PT_LOAD 節區，回傳實際填入的數量。
// 節區宣告的檔案範圍若超出 buffer 就整份拒絕，避免載入器讀到界外資料。
//
// Collect every PT_LOAD segment and return how many were written. If any
// segment's declared file range falls outside buffer the whole ELF is
// rejected, so a loader can never read past the end of what it read from disk.
std::size_t elf64_segments(const void* buffer, std::size_t size, std::uint16_t machine,
                           Elf64Segment* segments, std::size_t capacity);

// 計算所有 PT_LOAD 節區涵蓋的實體位址範圍；沒有可載入節區時回傳 false。
// Compute the physical address range every PT_LOAD segment covers; returns
// false when there is nothing to load.
bool elf64_physical_extent(const Elf64Segment* segments, std::size_t count,
                           std::uint64_t& lowest, std::uint64_t& end);

} // namespace shirley::boot
