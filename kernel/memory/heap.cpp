#include "shirley/heap.hpp"

#include <cstdint>
#include <limits>

namespace shirley::heap {
namespace {

// 每個區塊都以這個標頭開始，區塊一個接一個鋪滿整個堆積，因此從頭走訪標頭就
// 能走遍所有區塊。next 只在空閒區塊上有意義；被配置的區塊從 next 的位置開始
// 就是交給呼叫端的記憶體。
//
// Every block starts with this header, and the blocks tile the whole heap one
// after another, so walking the headers from the start visits every block.
// next means something only in a free block; in an allocated one, the memory
// handed to the caller starts where next would be.
struct Block {
    std::size_t size;
    std::uint64_t tag;
    Block* next;
};

constexpr std::size_t header_bytes = 16;
static_assert(sizeof(std::size_t) + sizeof(std::uint64_t) == header_bytes);
// 最小的區塊得放得下標頭與 next，再進位到對齊單位。比這更小的剩餘空間不會
// 被切成獨立區塊，而是併進相鄰的配置裡。
//
// The smallest block holds a header and next, rounded up to the alignment
// unit. A remainder smaller than this is never split into a block of its own;
// it is folded into the neighbouring allocation instead.
constexpr std::size_t min_block = 32;
static_assert(sizeof(Block) <= min_block);

// 標頭裡的標記。釋放時靠它分辨「這是一個被配置的區塊」與「這是空閒區塊或
// 根本不是區塊」，重複釋放因此被忽略而不是把同一塊放進串列兩次。
//
// The tag in a header. free() uses it to tell "an allocated block" apart from
// "a free block, or no block at all", so a double free is ignored rather than
// putting one block on the list twice.
constexpr std::uint64_t used_tag = 0x4445535553485248ull; // "HRHSUSED"
constexpr std::uint64_t free_tag = 0x4545524653485248ull; // "HRHSFREE"

std::uintptr_t heap_begin = 0;
std::uintptr_t heap_end = 0;
Block* free_head = nullptr;
std::size_t free_bytes = 0;

constexpr std::uintptr_t align_up(std::uintptr_t value, std::size_t alignment) {
    return (value + alignment - 1) & ~static_cast<std::uintptr_t>(alignment - 1);
}

Block* block_at(std::uintptr_t address) { return reinterpret_cast<Block*>(address); }
std::uintptr_t address_of(const Block* block) { return reinterpret_cast<std::uintptr_t>(block); }
std::uintptr_t end_of(const Block* block) { return address_of(block) + block->size; }

Block* make_free(std::uintptr_t address, std::size_t size, Block* next) {
    auto* block = block_at(address);
    block->size = size;
    block->tag = free_tag;
    block->next = next;
    return block;
}

// header 是不是一個真正被配置的區塊的開頭。標記可能剛好出現在使用者資料裡，
// 所以只看標記不夠；從頭走訪一次，確定它落在區塊邊界上。
//
// Whether header starts a block that really is allocated. A tag can appear in
// user data by chance, so the tag alone is not enough; walking from the start
// confirms it sits on a block boundary.
bool allocated_block(std::uintptr_t header) {
    for (auto cursor = heap_begin; cursor < heap_end;) {
        const auto* block = block_at(cursor);
        if (cursor == header) return block->tag == used_tag;
        if (cursor > header || block->size < min_block) return false;
        cursor += block->size;
    }
    return false;
}

} // namespace

void initialize(void* base, std::size_t bytes) {
    heap_begin = heap_end = 0;
    free_head = nullptr;
    free_bytes = 0;
    const auto raw = reinterpret_cast<std::uintptr_t>(base);
    if (base == nullptr || bytes > std::numeric_limits<std::uintptr_t>::max() - raw) return;
    const auto begin = align_up(raw, default_alignment);
    const auto end = (raw + bytes) & ~static_cast<std::uintptr_t>(default_alignment - 1);
    if (begin >= end || end - begin < min_block) return;
    heap_begin = begin;
    heap_end = end;
    free_head = make_free(begin, end - begin, nullptr);
    free_bytes = end - begin;
}

void* allocate(std::size_t bytes, std::size_t alignment) {
    if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) return nullptr;
    if (alignment < default_alignment) alignment = default_alignment;
    // 先擋掉不可能放得下的請求，後面的進位運算才不會溢位。
    // Turn away a request that can never fit first, so the rounding below
    // cannot overflow.
    if (bytes > heap_end - heap_begin || alignment > heap_end - heap_begin) return nullptr;
    const auto payload_bytes = align_up(bytes, default_alignment);

    for (Block** link = &free_head; *link != nullptr; link = &(*link)->next) {
        Block* current = *link;
        const auto start = address_of(current);
        const auto end = end_of(current);
        auto payload = align_up(start + header_bytes, alignment);
        // 對齊在前面留下的空隙要嘛是零，要嘛大到能自成一個空閒區塊；太小的
        // 空隙沒有地方記錄，所以把配置往後推到下一個對齊位置。
        //
        // The gap alignment leaves in front is either zero or large enough to
        // be a free block of its own. A smaller gap would have nowhere to be
        // recorded, so the allocation moves on to the next aligned address.
        if (payload - header_bytes != start && payload - header_bytes - start < min_block)
            payload = align_up(start + min_block + header_bytes, alignment);
        const auto block_start = payload - header_bytes;
        if (block_start >= end || end - payload < payload_bytes) continue;

        auto block_end = payload + payload_bytes;
        // 剩下的尾巴放不下一個區塊時，整段併進這次配置。
        // A tail too small to be a block is folded into this allocation.
        if (end - block_end < min_block) block_end = end;

        Block* next = current->next;
        if (block_end < end) next = make_free(block_end, end - block_end, next);
        if (block_start > start) {
            current->size = block_start - start;
            current->next = next;
        } else {
            *link = next;
        }

        auto* block = block_at(block_start);
        block->size = block_end - block_start;
        block->tag = used_tag;
        free_bytes -= block->size;
        return reinterpret_cast<void*>(payload);
    }
    return nullptr;
}

void free(void* pointer) {
    if (pointer == nullptr) return;
    const auto payload = reinterpret_cast<std::uintptr_t>(pointer);
    if (payload < heap_begin + header_bytes || payload >= heap_end ||
        (payload & (default_alignment - 1)) != 0)
        return;
    const auto header = payload - header_bytes;
    if (!allocated_block(header)) return;

    auto* block = block_at(header);
    block->tag = free_tag;
    free_bytes += block->size;

    // 依位址找出前後兩個空閒區塊，插進中間，再與緊鄰的那幾塊合併。
    // Find the free blocks on either side by address, link this one between
    // them, and merge with whichever of them touch it.
    Block* previous = nullptr;
    Block* next = free_head;
    while (next != nullptr && address_of(next) < header) {
        previous = next;
        next = next->next;
    }
    block->next = next;
    if (next != nullptr && end_of(block) == address_of(next)) {
        block->size += next->size;
        block->next = next->next;
    }
    if (previous == nullptr) {
        free_head = block;
    } else if (end_of(previous) == header) {
        previous->size += block->size;
        previous->next = block->next;
    } else {
        previous->next = block;
    }
}

std::size_t capacity() { return heap_end - heap_begin; }
std::size_t available() { return free_bytes; }

std::size_t largest_available() {
    std::size_t largest = 0;
    for (const Block* block = free_head; block != nullptr; block = block->next)
        if (block->size - header_bytes > largest) largest = block->size - header_bytes;
    return largest;
}

bool consistent() {
    std::size_t listed = 0;
    std::size_t listed_bytes = 0;
    std::uintptr_t previous_end = 0;
    for (const Block* block = free_head; block != nullptr; block = block->next) {
        const auto start = address_of(block);
        if (start < heap_begin || block->size < min_block || block->size > heap_end - start ||
            block->tag != free_tag || (previous_end != 0 && start <= previous_end))
            return false;
        previous_end = end_of(block);
        ++listed;
        listed_bytes += block->size;
    }

    std::size_t walked = 0;
    auto cursor = heap_begin;
    while (cursor < heap_end) {
        const auto* block = block_at(cursor);
        if (block->size < min_block || block->size > heap_end - cursor ||
            (block->tag != used_tag && block->tag != free_tag))
            return false;
        if (block->tag == free_tag) ++walked;
        cursor += block->size;
    }
    return cursor == heap_end && walked == listed && listed_bytes == free_bytes;
}

} // namespace shirley::heap
