#pragma once

#include "shirley/block_device.hpp"

namespace shirley::io {

class RamDisk final : public BlockDevice {
public:
    RamDisk(void* storage, std::size_t bytes, std::size_t block_bytes = 512);
    std::size_t block_size() const override;
    std::uint64_t block_count() const override;
    Result read_blocks(std::uint64_t first, std::size_t count, void* buffer) override;
    Result write_blocks(std::uint64_t first, std::size_t count, const void* buffer) override;

private:
    std::uint8_t* storage_;
    std::size_t bytes_;
    std::size_t block_bytes_;
};

} // namespace shirley::io
