#pragma once

#include <cstddef>

// 核心堆積：在一段呼叫端交給它的記憶體上做可變大小的配置。
//
// 堆積不自己去要頁面。核心把它放在 .bss 裡的一塊固定區域上，原因是 ARM64 的
// 行程位址空間只映射核心映像本身：從頁面配置器拿來的頁面，在 user 行程執行
// 期間的系統呼叫裡是碰不到的，而 .bss 永遠在核心映像之內。主機測試則把它放在
// 測試自己的陣列上。
//
// 演算法是依位址排序的空閒串列加上首次適配：配置時切開第一個放得下的空閒區塊，
// 釋放時與前後相鄰的空閒區塊合併。在這個規模下這樣就夠了，而且每一步都看得懂。
//
// The kernel heap: variable-sized allocations carved out of memory the caller
// hands it.
//
// The heap does not fetch pages itself. The kernel places it on a fixed region
// in .bss, because an ARM64 process address space maps only the kernel image:
// a page taken from the page allocator would be out of reach inside a syscall
// made while a user process runs, whereas .bss always lies inside the kernel
// image. Host tests place it on an array of their own.
//
// The algorithm is an address-ordered free list with first fit: an allocation
// splits the first free block that is large enough, and a free merges with
// whichever neighbours are free. At this scale that is enough, and every step
// of it can be followed by eye.
namespace shirley::heap {

// 未指定時的對齊，也是所有區塊大小的單位。
// The alignment used when none is asked for, and the unit of every block size.
constexpr std::size_t default_alignment = 16;

// 以 [base, base + bytes) 重新建立堆積，丟棄之前的一切配置。區域太小時堆積
// 維持為空，之後的配置都會失敗。
//
// Rebuild the heap over [base, base + bytes), discarding every earlier
// allocation. A region too small to use leaves the heap empty, and every
// allocation afterwards fails.
void initialize(void* base, std::size_t bytes);

// 配置至少 bytes 個位元組，起始位址是 alignment 的倍數。alignment 必須是 2 的
// 次方；失敗、bytes 為 0 或 alignment 不合法時回傳 nullptr。內容不會被清空。
//
// Allocate at least bytes bytes starting at a multiple of alignment, which has
// to be a power of two. Returns nullptr on failure, for zero bytes, or for an
// invalid alignment. The memory is not cleared.
void* allocate(std::size_t bytes, std::size_t alignment = default_alignment);

// 歸還 allocate() 給出的指標。nullptr 不做任何事；不是堆積給出的指標、或已經
// 釋放過的指標也被忽略，就像 memory::free_page() 一樣，而不是破壞空閒串列。
//
// Return a pointer allocate() handed out. nullptr does nothing; a pointer the
// heap never handed out, or one already freed, is ignored just as
// memory::free_page() ignores one, rather than corrupting the free list.
void free(void* pointer);

// 堆積可管理的位元組數、目前還沒被配置的位元組數（含區塊標頭），以及最大的
// 那一塊空閒區塊能給出多少位元組。最後一項比總數更能回答「這次配置會不會
// 成功」：碎片化時兩者可以差很多。
//
// The bytes the heap manages, the bytes not currently allocated (block headers
// included), and how much the largest free block can hand out. The last one
// answers "will this allocation succeed" better than the total does: under
// fragmentation the two can be far apart.
std::size_t capacity();
std::size_t available();
std::size_t largest_available();

// 檢查空閒串列：依位址排序、彼此不相鄰（相鄰的應該早已合併）、全部落在堆積
// 之內，而且所有區塊加起來剛好鋪滿整個堆積。供測試與除錯使用。
//
// Check the free list: sorted by address, no two adjacent (adjacent blocks
// should have merged), all inside the heap, and every block together tiling
// the heap exactly. For tests and debugging.
bool consistent();

} // namespace shirley::heap
