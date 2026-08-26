#include "internal.hpp"

namespace shirley::arch::x86_64 {
namespace {

// 長模式的 TSS 只用來提供特權層轉換時的堆疊指標。
// In long mode the TSS only supplies the stack pointers used on a
// privilege-level transition.
struct [[gnu::packed]] TaskStateSegment {
    std::uint32_t reserved0;
    std::uint64_t rsp[3];
    std::uint64_t reserved1;
    std::uint64_t ist[7];
    std::uint64_t reserved2;
    std::uint16_t reserved3;
    std::uint16_t io_map_base;
};

struct [[gnu::packed]] DescriptorPointer {
    std::uint16_t limit;
    std::uint64_t base;
};

// 段描述元：null、核心程式碼/資料、使用者資料/程式碼，以及佔兩格的 TSS。
// The descriptors: null, kernel code/data, user data/code, and the TSS, which
// occupies two slots.
constexpr std::size_t entry_count = 7;
alignas(16) std::uint64_t entries[entry_count]{};
alignas(16) TaskStateSegment task_state{};
DescriptorPointer pointer{};

// 依 Intel 手冊的欄位配置組出 64 位元系統描述元的兩個字組。
// Build the two words of a 64-bit system descriptor per the Intel manual's
// field layout.
void set_tss_descriptor(std::size_t index, std::uint64_t base, std::uint32_t limit) {
    // type=9（可用的 64 位元 TSS）、present=1、DPL=0。
    // type=9 (available 64-bit TSS), present=1, DPL=0.
    entries[index] = (limit & 0xffffull) | ((base & 0xffffffull) << 16) | (0x89ull << 40) |
                     (((limit >> 16) & 0xfull) << 48) | (((base >> 24) & 0xffull) << 56);
    entries[index + 1] = (base >> 32) & 0xffffffffull;
}

} // namespace

void gdt_initialize() {
    entries[0] = 0;
    // 核心程式碼，L=1、DPL=0 / kernel code, L=1, DPL=0
    entries[1] = 0x00af9a000000ffffull;
    // 核心資料，DPL=0 / kernel data, DPL=0
    entries[2] = 0x00cf92000000ffffull;
    // 使用者資料，DPL=3 / user data, DPL=3
    entries[3] = 0x00cff2000000ffffull;
    // 使用者程式碼，L=1、DPL=3 / user code, L=1, DPL=3
    entries[4] = 0x00affa000000ffffull;

    // io_map_base 指到 TSS 結尾表示沒有 I/O 權限點陣圖。
    // Pointing io_map_base at the end of the TSS means there is no I/O
    // permission bitmap.
    task_state = {};
    task_state.io_map_base = sizeof(TaskStateSegment);
    set_tss_descriptor(5, reinterpret_cast<std::uint64_t>(&task_state), sizeof(TaskStateSegment) - 1);

    pointer = {static_cast<std::uint16_t>(sizeof(entries) - 1), reinterpret_cast<std::uint64_t>(entries)};
    x86_64_load_gdt(&pointer, kernel_code_selector, kernel_data_selector);
    x86_64_load_tss(tss_selector);
}

void gdt_set_kernel_stack(std::uint64_t stack_top) { task_state.rsp[0] = stack_top; }

} // namespace shirley::arch::x86_64
