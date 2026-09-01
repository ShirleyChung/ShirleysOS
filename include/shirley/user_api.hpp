#pragma once

#include <cstdint>
#include <cstddef>

namespace shirley::user_api {

constexpr std::size_t path_capacity = 128;
constexpr std::size_t name_capacity = 56;

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
struct BlockReadRequest {
    const char* path;
    std::uint64_t block;
    void* buffer;
    std::uint64_t capacity;
};

} // namespace shirley::user_api
