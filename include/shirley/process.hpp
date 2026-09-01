#pragma once

#include <cstddef>
#include <cstdint>

// 一個使用者行程的檔案描述子表。系統呼叫層是 ShirleyOS 原生 ABI 與通用核心的
// 交界，這個模組把使用者看到的小整數描述子接到底下的東西：0/1/2 是標準輸入、
// 輸出、錯誤，接到主控台；open() 開出來的描述子則接到一個 VFS 描述子。行程一
// 次只有一個，因此描述子表是一份固定大小的靜態表。
//
// One user process's file descriptor table. The syscall layer is the boundary
// between ShirleyOS's native ABI and the generic kernel, and this module maps
// the small integer descriptors a program sees onto what is underneath: 0, 1,
// and 2 are standard input, output, and error wired to the console, while a
// descriptor from open() is backed by a VFS descriptor. There is one process at
// a time, so the table is a single fixed-size static.
namespace shirley::process {

// 一個行程能同時開啟的描述子數。固定大小：核心沒有動態配置器。
// How many descriptors a process can hold open at once. Fixed: the kernel has
// no dynamic allocator.
constexpr int max_open_files = 16;

// 把描述子表重設成三個標準串流（0=stdin、1=stdout、2=stderr）。在載入好的
// 程式進入使用者空間之前呼叫。
//
// Reset the descriptor table to the three standard streams (0=stdin, 1=stdout,
// 2=stderr). Call before a loaded program enters userspace.
void reset();

// 關閉程式離開時還開著的描述子，避免共用的 VFS 描述子表外洩。行程結束時呼叫。
// Close any descriptor a program left open so the shared VFS descriptor table
// does not leak. Called when a process exits.
void teardown();

// 系統呼叫底下的檔案操作。每個都回傳交還給使用者程式的值：成功時是位元組數
// 或描述子，失敗時是負數。
//
// The file operations behind the syscalls. Each returns the value handed back
// to the user program: a count or descriptor on success, a negative value on
// failure.
long long open(const char* path, std::uint64_t flags);
long long close(int descriptor);
long long read(int descriptor, void* buffer, std::size_t length);
long long write(int descriptor, const void* buffer, std::size_t length);

} // namespace shirley::process
