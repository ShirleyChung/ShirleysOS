#pragma once

#include "shirley/boot_info.hpp"

namespace shirley {

// ShirleyOS 開機載入器與核心之間的交接結構。開機載入器是獨立的二進位檔，
// 因此這個結構是兩者之間的 ABI：magic 與 size 讓核心能確認自己收到的
// 確實是本專案的載入器產生的資料，而不是韌體殘留的指標。
//
// The handoff structure between the ShirleyOS boot loader and the kernel. The
// loader is a separate binary, so this struct is the ABI between them: the
// magic and size let the kernel confirm it really received data from this
// project's loader rather than a stale firmware pointer.
struct BootHandoff {
    // "SHIRLEY0" 的 ASCII 編碼，以小端序存放。
    // The ASCII of "SHIRLEY0", stored little-endian.
    static constexpr std::uint64_t magic_value = 0x3059454c52494853ull;
    static constexpr std::uint32_t current_version = 1;

    std::uint64_t magic = magic_value;
    std::uint32_t version = current_version;
    std::uint32_t size = sizeof(BootHandoff);
    BootInfo info{};
};

// 確認交接結構可用。size 只檢查下限，之後新增欄位時舊核心仍能安全讀取前段。
// Check that a handoff structure is usable. Only a lower bound is enforced on
// size, so an older kernel can still safely read the leading fields after new
// ones are appended.
inline bool boot_handoff_valid(const BootHandoff* handoff) {
    if (handoff == nullptr) return false;
    if (handoff->magic != BootHandoff::magic_value) return false;
    if (handoff->version != BootHandoff::current_version) return false;
    if (handoff->size < sizeof(BootHandoff)) return false;
    return handoff->info.version == BootInfo::current_version;
}

} // namespace shirley
