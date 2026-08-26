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

void cpu_initialize();
const char* cpu_vendor_string();

void exception_initialize();
bool exception_register(unsigned vector, InterruptHandler handler, void* context);
void exception_unregister(unsigned vector);

// 啟用 MMU 並切換到指定的轉換表；ARM64 開機時 MMU 尚未啟用。
// Enable the MMU and switch to the given translation table. On ARM64 the MMU
// is still off when the kernel starts.
bool mmu_enable(std::uint64_t translation_table);

} // namespace shirley::arch::arm64

extern "C" {
// 由 exception.S 提供的 2 KiB 對齊向量表。
// The 2 KiB-aligned vector table provided by exception.S.
extern const char arm64_exception_vectors[];
[[noreturn]] void arm64_enter_userspace(std::uint64_t entry, std::uint64_t stack);
}
