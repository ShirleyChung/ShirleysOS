#pragma once

#include "shirley/arch.hpp"
#include "shirley/arch/arm64/exception.hpp"

namespace shirley::arch::arm64 {

// 例外發生時由 exception.S 建立的暫存器影像。
// 欄位順序與偏移量必須與 exception.S 中的 stp/str 完全一致。
//
// The register image exception.S builds on the stack. The field order and
// offsets must match the stp/str instructions in exception.S exactly.
struct ExceptionFrame {
    // 通用暫存器 x0 到 x30 / general purpose registers x0 through x30
    std::uint64_t x[31];
    std::uint64_t stack_pointer;
    // 例外返回位址 / exception link register
    std::uint64_t elr;
    // 例外前的處理器狀態 / saved processor state
    std::uint64_t spsr;
    // 例外症狀暫存器 / exception syndrome register
    std::uint64_t esr;
    std::uint64_t fault_address;
    // 向量表入口編號 / index into the exception vector table
    std::uint64_t vector;
    std::uint64_t reserved;
};
static_assert(sizeof(ExceptionFrame) == 304, "exception.S 使用固定的 304 位元組框架");

// 進入 EL0 前保存的核心執行狀態，讓 exit 系統呼叫能跳回這裡返回。
// 欄位順序與偏移量必須與 exception.S 的 arm64_save_and_enter/restore_and_exit
// 完全一致。
//
// The kernel execution state saved before entering EL0, so the exit syscall can
// jump back here and return. The field order and offsets must match
// arm64_save_and_enter/restore_and_exit in exception.S exactly.
struct UserContext {
    // 被呼叫者保存暫存器 x19 到 x28 / callee-saved registers x19 through x28
    std::uint64_t x[10];   // 0..72
    std::uint64_t fp;      // 80，x29
    std::uint64_t lr;      // 88，x30，enter 的返回位址 / the return address into enter
    std::uint64_t sp;      // 96
    std::uint64_t daif;    // 104，核心的中斷遮罩狀態 / the kernel interrupt mask state
};

void cpu_initialize();
const char* cpu_vendor_string();

void exception_initialize();
bool exception_register(unsigned vector, InterruptHandler handler, void* context);
void exception_unregister(unsigned vector);

// 啟用 MMU 並切換到指定的轉換表；ARM64 開機時 MMU 尚未啟用。
// Enable the MMU and switch to the given translation table. On ARM64 the MMU
// is still off when the kernel starts.
bool mmu_enable(std::uint64_t translation_table);
// 目前 MMU 是否已啟用（SCTLR_EL1 的 M 位元）。
// Whether the MMU is currently enabled (the M bit of SCTLR_EL1).
bool mmu_enabled();
// 關閉 MMU 與快取，回到核心開機時的無轉換狀態，讓剛才的使用者轉換表可以安全
// 釋放。核心以恆等映射執行，因此關閉後位址不變。
//
// Turn the MMU and caches off, returning to the kernel's boot-time
// no-translation state so the user translation table can be freed safely. The
// kernel runs identity-mapped, so addresses are unchanged once it is off.
bool mmu_disable();

} // namespace shirley::arch::arm64

extern "C" {
// 由 exception.S 提供的 2 KiB 對齊向量表。
// The 2 KiB-aligned vector table provided by exception.S.
extern const char arm64_exception_vectors[];
// 保存核心狀態到 *context，然後以 ERET 進入 EL0。使用者行程呼叫 exit 時由
// arm64_restore_and_exit 跳回本函式的呼叫端，回傳值即為結束碼。
//
// Save the kernel state into *context, then ERET into EL0. When the user
// process calls exit, arm64_restore_and_exit jumps back to this function's
// caller and the return value is the exit status.
long arm64_save_and_enter(std::uint64_t entry, std::uint64_t stack,
                          shirley::arch::arm64::UserContext* context);
[[noreturn]] void arm64_restore_and_exit(shirley::arch::arm64::UserContext* context, long status);
}
