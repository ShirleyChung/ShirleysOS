#include "shirley/process.hpp"

#include "shirley/console.hpp"
#include "shirley/io.hpp"
#include "shirley/vfs.hpp"

namespace shirley::process {
namespace {

// 一個描述子的種類。標準串流接主控台；一般檔案接一個 VFS 描述子。
// What one descriptor is. A standard stream reaches the console; an ordinary
// file is backed by a VFS descriptor.
enum class Kind : std::uint8_t { Unused, StandardInput, StandardOutput, File };

struct Entry {
    Kind kind = Kind::Unused;
    // Kind::File 時保存底層的 VFS 描述子。
    // Holds the underlying VFS descriptor when Kind::File.
    int backing = -1;
};

Entry table[max_open_files]{};

bool in_range(int descriptor) { return descriptor >= 0 && descriptor < max_open_files; }

// 使用者程式的 open 旗標，與 libc 的 fcntl.h 一致。
// The user program's open flags, matching libc's fcntl.h.
constexpr std::uint64_t open_read_only = 0;
constexpr std::uint64_t open_write_only = 1;
constexpr std::uint64_t open_read_write = 2;

} // namespace

void reset() {
    for (auto& entry : table) entry = {};
    table[0] = {Kind::StandardInput, -1};
    table[1] = {Kind::StandardOutput, -1};
    table[2] = {Kind::StandardOutput, -1};
}

void teardown() {
    for (auto& entry : table) {
        if (entry.kind == Kind::File) vfs::close(entry.backing);
        entry = {};
    }
}

long long open(const char* path, std::uint64_t flags) {
    if (path == nullptr) return -1;
    vfs::OpenFlags access = vfs::OpenFlags::Read;
    if (flags == open_write_only) access = vfs::OpenFlags::Write;
    else if (flags == open_read_write) access = vfs::OpenFlags::Read | vfs::OpenFlags::Write;
    else if (flags != open_read_only) return -1;
    // 使用者程式還沒有自己的工作目錄，因此路徑以根目錄為基準解析。
    // A user program has no working directory of its own yet, so a path is
    // resolved against the root.
    const int backing = vfs::open(path, access);
    if (backing < 0) return backing;
    for (int descriptor = 3; descriptor < max_open_files; ++descriptor) {
        if (table[descriptor].kind != Kind::Unused) continue;
        table[descriptor] = {Kind::File, backing};
        return descriptor;
    }
    // 描述子表滿了：關掉剛開的 VFS 描述子，別讓它外洩。
    // The table is full: close the VFS descriptor just opened so it does not
    // leak.
    vfs::close(backing);
    return -1;
}

long long close(int descriptor) {
    if (!in_range(descriptor) || table[descriptor].kind == Kind::Unused) return -1;
    if (table[descriptor].kind == Kind::File) vfs::close(table[descriptor].backing);
    table[descriptor] = {};
    return 0;
}

long long read(int descriptor, void* buffer, std::size_t length) {
    if (!in_range(descriptor)) return -1;
    if (length == 0) return 0;
    if (buffer == nullptr) return -1;
    switch (table[descriptor].kind) {
    case Kind::StandardInput: {
        const auto result = io::read_standard_input(buffer, length);
        return result ? static_cast<long long>(result.transferred) : -1;
    }
    case Kind::File: {
        const auto result = vfs::read(table[descriptor].backing, buffer, length);
        return result ? static_cast<long long>(result.transferred) : -1;
    }
    default:
        // 標準輸出／錯誤與未使用的描述子讀不出東西。
        // Standard output/error and unused descriptors cannot be read.
        return -1;
    }
}

long long write(int descriptor, const void* buffer, std::size_t length) {
    if (!in_range(descriptor)) return -1;
    if (length == 0) return 0;
    if (buffer == nullptr) return -1;
    switch (table[descriptor].kind) {
    case Kind::StandardOutput:
        console::write(static_cast<const char*>(buffer), length);
        return static_cast<long long>(length);
    case Kind::File: {
        const auto result = vfs::write(table[descriptor].backing, buffer, length);
        return result ? static_cast<long long>(result.transferred) : -1;
    }
    default:
        // 標準輸入與未使用的描述子寫不進去。
        // Standard input and unused descriptors cannot be written.
        return -1;
    }
}

} // namespace shirley::process
