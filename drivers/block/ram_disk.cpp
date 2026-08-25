#include "shirley/ram_disk.hpp"

#include <limits>

namespace shirley::io {
namespace {
void copy_bytes(void* destination, const void* source, std::size_t count) {
    auto* out = static_cast<std::uint8_t*>(destination);
    const auto* in = static_cast<const std::uint8_t*>(source);
    for (std::size_t i = 0; i < count; ++i) out[i] = in[i];
}
} // namespace

RamDisk::RamDisk(void* storage, std::size_t bytes, std::size_t block_bytes)
    : storage_(static_cast<std::uint8_t*>(storage)), bytes_(bytes), block_bytes_(block_bytes) {}

std::size_t RamDisk::block_size() const { return block_bytes_; }

std::uint64_t RamDisk::block_count() const {
    return storage_ == nullptr || block_bytes_ == 0 ? 0 : bytes_ / block_bytes_;
}

Result RamDisk::read_blocks(std::uint64_t first, std::size_t count, void* buffer) {
    if (count != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    if (block_bytes_ == 0 || storage_ == nullptr) return {0, Error::DeviceError};
    if (first > block_count() || count > block_count() - first ||
        count > std::numeric_limits<std::size_t>::max() / block_bytes_) return {0, Error::OutOfRange};
    const auto bytes = count * block_bytes_;
    copy_bytes(buffer, storage_ + static_cast<std::size_t>(first) * block_bytes_, bytes);
    return {bytes, Error::None};
}

Result RamDisk::write_blocks(std::uint64_t first, std::size_t count, const void* buffer) {
    if (count != 0 && buffer == nullptr) return {0, Error::InvalidArgument};
    if (block_bytes_ == 0 || storage_ == nullptr) return {0, Error::DeviceError};
    if (first > block_count() || count > block_count() - first ||
        count > std::numeric_limits<std::size_t>::max() / block_bytes_) return {0, Error::OutOfRange};
    const auto bytes = count * block_bytes_;
    copy_bytes(storage_ + static_cast<std::size_t>(first) * block_bytes_, buffer, bytes);
    return {bytes, Error::None};
}

} // namespace shirley::io
