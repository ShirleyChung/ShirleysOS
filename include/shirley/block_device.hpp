#pragma once

#include "shirley/io.hpp"

#include <cstddef>
#include <cstdint>

namespace shirley::io {

class BlockDevice {
public:
    virtual std::size_t block_size() const = 0;
    virtual std::uint64_t block_count() const = 0;
    virtual Result read_blocks(std::uint64_t first, std::size_t count, void* buffer) = 0;
    virtual Result write_blocks(std::uint64_t first, std::size_t count, const void* buffer) = 0;
protected:
    ~BlockDevice() = default;
};

} // namespace shirley::io
