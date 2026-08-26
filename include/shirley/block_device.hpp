#pragma once

#include "shirley/io.hpp"

#include <cstddef>
#include <cstdint>

namespace shirley::io {

// 以固定大小區塊為單位存取的儲存裝置。
// A storage device addressed in fixed-size blocks.
class BlockDevice {
public:
    virtual std::size_t block_size() const = 0;
    virtual std::uint64_t block_count() const = 0;
    // first 與 count 超出裝置範圍時回傳 OutOfRange，而不是部分傳輸。
    // A first/count pair outside the device returns OutOfRange rather than a
    // partial transfer.
    virtual Result read_blocks(std::uint64_t first, std::size_t count, void* buffer) = 0;
    virtual Result write_blocks(std::uint64_t first, std::size_t count, const void* buffer) = 0;
protected:
    ~BlockDevice() = default;
};

} // namespace shirley::io
