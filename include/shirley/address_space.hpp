#pragma once

#include <cstdint>

namespace shirley::memory {

using VirtualAddress = std::uint64_t;
using PhysicalAddress = std::uint64_t;

enum class PageFlags : std::uint32_t {
    None = 0,
    Read = 1u << 0,
    Write = 1u << 1,
    Execute = 1u << 2,
    User = 1u << 3,
};

class AddressSpace {
public:
    virtual ~AddressSpace() = default;
    virtual bool map(VirtualAddress, PhysicalAddress, PageFlags) = 0;
    virtual bool unmap(VirtualAddress) = 0;
    virtual PhysicalAddress translate(VirtualAddress) const = 0;
};

} // namespace shirley::memory
