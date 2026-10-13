#pragma once

#include <cstdint>
#include <cstddef>

namespace shirley::user_api {

constexpr std::size_t path_capacity = 512;
constexpr std::size_t name_capacity = 256;

struct NodeInfo {
    std::uint64_t size;
    std::uint64_t entries;
    std::uint32_t type;
    char name[name_capacity];
    char path[path_capacity];
    char filesystem[16];
};

struct UptimeInfo { std::uint64_t ticks, frequency; };
struct MountInfo { char path[path_capacity]; char filesystem[16]; };
// mount_device 的引數。兩個路徑都必須是絕對路徑；reason 是 capacity 位元組的
// 緩衝區，失敗時放進以 null 結尾的原因。
// The arguments of mount_device. Both paths must be absolute; reason is a
// buffer of capacity bytes that receives a null-terminated reason on failure.
struct MountRequest {
    const char* device;
    const char* path;
    char* reason;
    std::uint64_t capacity;
};
struct BlockReadRequest {
    const char* path;
    std::uint64_t block;
    void* buffer;
    std::uint64_t capacity;
};

} // namespace shirley::user_api
