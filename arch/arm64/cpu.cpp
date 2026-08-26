#include "internal.hpp"

namespace shirley::arch::arm64 {
namespace {

std::uint64_t read_midr() {
    std::uint64_t value;
    asm volatile("mrs %0, midr_el1" : "=r"(value));
    return value;
}

std::uint64_t read_cpacr() {
    std::uint64_t value;
    asm volatile("mrs %0, cpacr_el1" : "=r"(value));
    return value;
}

void write_cpacr(std::uint64_t value) {
    asm volatile("msr cpacr_el1, %0; isb" : : "r"(value) : "memory");
}

// CPACR_EL1.FPEN：0b11 表示 EL0 與 EL1 都可以存取 FP/SIMD。
// CPACR_EL1.FPEN: 0b11 lets both EL0 and EL1 use FP/SIMD.
constexpr std::uint64_t cpacr_fpen = 3ull << 20;

const char* vendor = "unknown";

// MIDR_EL1 的 implementer 欄位是 JEP106 廠商代碼。
// The implementer field of MIDR_EL1 is a JEP106 vendor code.
const char* implementer_name(std::uint8_t implementer) {
    switch (implementer) {
        case 0x41: return "ARM";
        case 0x42: return "Broadcom";
        case 0x43: return "Cavium";
        case 0x44: return "DEC";
        case 0x46: return "Fujitsu";
        case 0x48: return "HiSilicon";
        case 0x49: return "Infineon";
        case 0x4e: return "NVIDIA";
        case 0x50: return "Applied Micro";
        case 0x51: return "Qualcomm";
        case 0x53: return "Samsung";
        case 0x56: return "Marvell";
        case 0x61: return "Apple";
        case 0x66: return "Faraday";
        case 0x69: return "Intel";
        case 0x6d: return "Microsoft";
        case 0xc0: return "Ampere";
        default: return "unknown";
    }
}

} // namespace

void cpu_initialize() {
    vendor = implementer_name(static_cast<std::uint8_t>((read_midr() >> 24) & 0xff));
    // 核心本身以 -mgeneral-regs-only 建置，但使用者程式需要 FP/SIMD。
    // The kernel itself is built with -mgeneral-regs-only, but user programs
    // need FP/SIMD.
    write_cpacr(read_cpacr() | cpacr_fpen);
}

const char* cpu_vendor_string() { return vendor; }

} // namespace shirley::arch::arm64
