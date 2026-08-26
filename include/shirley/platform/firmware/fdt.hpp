#pragma once

#include "shirley/boot_info.hpp"

// Flattened Device Tree（裝置樹二進位格式）。QEMU virt 以 x0 傳入 DTB 位址，
// UEFI 與 U-Boot 也使用同一種格式，因此放在共用的韌體格式層。
// 所有解析都對來源資料做邊界檢查，因為 DTB 由韌體提供而非核心產生。
//
// The flattened device tree binary format. QEMU virt passes the DTB address in
// x0, and UEFI and U-Boot use the same format, so it belongs in the shared
// firmware format layer. Every parse bounds-checks its input, because the blob
// comes from firmware rather than from the kernel.
namespace shirley::platform::firmware {

// DTB 開頭的識別值。
// The magic value at the start of a DTB.
constexpr std::uint32_t fdt_magic = 0xd00dfeedu;

// 確認指標指向結構合理的 DTB。
// Check that the pointer refers to a structurally sane DTB.
bool fdt_valid(const void* blob);
// DTB 自身佔用的位元組數；不合法時回傳 0。
// How many bytes the DTB itself occupies; 0 when it is not valid.
std::uint64_t fdt_total_size(const void* blob);

// 讀出 /memory 節點與記憶體保留區段，寫入通用區段陣列並回傳寫入數量。
// /memory 節點會標記為 Usable，保留區段標記為 Reserved。
//
// Read the /memory node and the reservation block into neutral memory regions
// and return how many were written. The /memory node becomes Usable and the
// reservation block becomes Reserved.
std::uint64_t fdt_memory_map(const void* blob, BootMemoryRegion* regions, std::uint64_t capacity);

} // namespace shirley::platform::firmware
