#pragma once

#include "shirley/address_space.hpp"
#include "shirley/boot/elf64.hpp"

#include <cstddef>
#include <cstdint>

namespace shirley::user {

// 一個 user 映像的大小上限。ELF loader 要走 program header，因此需要一整份
// 連續的映像，而核心沒有堆積可以配置任意大小的緩衝區；程式超過這個大小會
// 被明確拒絕，而不是安靜地被截斷成一份走不通的 ELF。
//
// 換掉這個限制的方法不是把數字改大，而是讓載入器直接把每個節區讀進它自己
// 那幾頁——那要等到有 demand paging 的時候。
//
// The largest user image that can be loaded. The ELF loader walks program
// headers and therefore needs one contiguous image, and the kernel has no heap
// to size a buffer from. A program past this size is refused plainly rather
// than quietly truncated into an ELF that cannot be walked.
//
// The way past this limit is not a bigger number but a loader that reads each
// segment straight into its own pages, which waits for demand paging.
constexpr std::size_t max_image_bytes = 64 * 1024;

struct Image {
    std::uint64_t entry = 0;
    std::uint64_t stack = 0;
    std::uint64_t stack_bottom = 0;
    std::size_t segment_count = 0;
};

// 載入靜態 user ELF：配置並映射 PT_LOAD 頁面、複製檔案內容、清除 .bss，
// 並配置一頁可寫入的初始 user stack。
// Load a static user ELF: allocate and map PT_LOAD pages, copy file contents,
// clear .bss, and allocate one writable initial user-stack page.
bool load_elf(const void* image, std::size_t size, std::uint16_t machine,
              memory::AddressSpace& address_space, Image& result);

// 從 VFS 讀出一個路徑上的 ELF、在自己的位址空間裡執行它，並在它呼叫 exit 之後
// 返回。成功執行回傳 true，並把程式的結束碼寫進 *status（status 可為 null）；
// 程式不存在、太大、不是這個架構的 ELF，或位址空間建不起來時回傳 false。
//
// 這是核心第一次以「檔案」而不是「連結進來的位元組」看待一個程式：路徑由
// VFS 解析，內容由檔案系統讀出，載入器只認得那份映像。行程結束後控制權會回到
// 這裡，位址空間被拆除，呼叫端（shell）因此能再度取得提示符。
//
// Read the ELF at a path through the VFS, run it in its own address space, and
// return once it calls exit. Returns true when it ran, writing the program's
// exit status into *status (which may be null); returns false when the program
// does not exist, is too large, is not an ELF for this architecture, or the
// address space cannot be built.
//
// This is the first time the kernel treats a program as a file rather than as
// bytes linked into itself: the VFS resolves the path, the file system reads
// the content, and the loader knows nothing but that image. Control returns
// here once the process exits and its address space is torn down, so the caller
// (the shell) gets its prompt back.
bool launch(const char* path, int* status = nullptr);

} // namespace shirley::user
