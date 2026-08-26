#include "internal.hpp"

namespace shirley::arch::x86_64 {
namespace {

struct CpuidResult { std::uint32_t eax, ebx, ecx, edx; };

CpuidResult cpuid(std::uint32_t leaf, std::uint32_t sub_leaf = 0) {
    CpuidResult result{};
    asm volatile("cpuid"
                 : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
                 : "a"(leaf), "c"(sub_leaf));
    return result;
}

std::uint64_t read_msr(std::uint32_t msr) {
    std::uint32_t low = 0, high = 0;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<std::uint64_t>(high) << 32) | low;
}

void write_msr(std::uint32_t msr, std::uint64_t value) {
    asm volatile("wrmsr" : : "c"(msr), "a"(static_cast<std::uint32_t>(value)),
                             "d"(static_cast<std::uint32_t>(value >> 32)));
}

std::uint64_t read_cr0() { std::uint64_t v; asm volatile("mov %%cr0, %0" : "=r"(v)); return v; }
void write_cr0(std::uint64_t v) { asm volatile("mov %0, %%cr0" : : "r"(v) : "memory"); }
std::uint64_t read_cr4() { std::uint64_t v; asm volatile("mov %%cr4, %0" : "=r"(v)); return v; }
void write_cr4(std::uint64_t v) { asm volatile("mov %0, %%cr4" : : "r"(v) : "memory"); }

constexpr std::uint32_t msr_efer = 0xc0000080;
constexpr std::uint64_t efer_nxe = 1ull << 11;
constexpr std::uint64_t cr0_monitor_coprocessor = 1ull << 1;
constexpr std::uint64_t cr0_emulation = 1ull << 2;
constexpr std::uint64_t cr0_write_protect = 1ull << 16;
constexpr std::uint64_t cr4_osfxsr = 1ull << 9;
constexpr std::uint64_t cr4_osxmmexcpt = 1ull << 10;

// CPUID 廠商字串固定 12 個字元，額外保留結尾。
// The CPUID vendor string is exactly 12 characters, plus a terminator.
char vendor[13] = "unknown";
bool nx_supported = false;
bool sse_enabled = false;

// 將 CPUID 回傳的三個暫存器接成廠商字串。
// Stitch the three CPUID registers together into the vendor string.
void store_vendor(const CpuidResult& info) {
    const std::uint32_t words[3] = {info.ebx, info.edx, info.ecx};
    for (unsigned word = 0; word < 3; ++word)
        for (unsigned byte = 0; byte < 4; ++byte)
            vendor[word * 4 + byte] = static_cast<char>((words[word] >> (byte * 8)) & 0xff);
    vendor[12] = '\0';
}

} // namespace

void cpu_initialize() {
    const auto identification = cpuid(0);
    if (identification.eax >= 1 || identification.ebx != 0) store_vendor(identification);

    // 讓核心也受唯讀分頁保護，避免誤寫使用者唯讀頁面。
    // Make the kernel obey read-only page permissions too, so it cannot
    // accidentally write a user page that is mapped read-only.
    write_cr0(read_cr0() | cr0_write_protect);

    // 開機載入器只設定了長模式必要的位元，這裡補上 SSE 與 NX。
    // The boot loader only set the bits long mode requires; NX and SSE are
    // enabled here.
    const auto extended = cpuid(0x80000001);
    if ((extended.edx & (1u << 20)) != 0) {
        nx_supported = true;
        write_msr(msr_efer, read_msr(msr_efer) | efer_nxe);
    }
    const auto features = cpuid(1);
    if ((features.edx & (1u << 25)) != 0) {
        write_cr0((read_cr0() & ~cr0_emulation) | cr0_monitor_coprocessor);
        write_cr4(read_cr4() | cr4_osfxsr | cr4_osxmmexcpt);
        sse_enabled = true;
    }
}

const char* cpu_vendor_string() { return vendor; }
bool cpu_nx_supported() { return nx_supported; }
bool cpu_sse_enabled() { return sse_enabled; }

} // namespace shirley::arch::x86_64
