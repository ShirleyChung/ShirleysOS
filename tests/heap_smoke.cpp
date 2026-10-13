#include "shirley/heap.hpp"

#include <cassert>
#include <cstdint>

namespace {

namespace heap = shirley::heap;

alignas(4096) unsigned char arena[64 * 1024];

bool aligned(const void* pointer, std::size_t alignment) {
    return (reinterpret_cast<std::uintptr_t>(pointer) & (alignment - 1)) == 0;
}

void test_empty_regions() {
    heap::initialize(nullptr, 4096);
    assert(heap::capacity() == 0 && heap::allocate(1) == nullptr);
    // 小到放不下一個區塊的區域等於沒有堆積。
    // A region too small for one block is no heap at all.
    heap::initialize(arena, 8);
    assert(heap::capacity() == 0 && heap::allocate(1) == nullptr);
    assert(heap::consistent());
}

void test_allocate_and_merge() {
    heap::initialize(arena, sizeof(arena));
    assert(heap::capacity() == sizeof(arena));
    assert(heap::available() == heap::capacity());
    assert(heap::consistent());

    assert(heap::allocate(0) == nullptr);
    assert(heap::allocate(16, 3) == nullptr);
    assert(heap::allocate(sizeof(arena)) == nullptr);

    void* a = heap::allocate(100);
    void* b = heap::allocate(200);
    void* c = heap::allocate(300);
    assert(a != nullptr && b != nullptr && c != nullptr);
    assert(aligned(a, 16) && aligned(b, 16) && aligned(c, 16));
    // 配置出來的記憶體不重疊。 / Allocations do not overlap.
    auto* bytes_a = static_cast<unsigned char*>(a);
    auto* bytes_b = static_cast<unsigned char*>(b);
    assert(bytes_a + 100 <= bytes_b);
    for (int i = 0; i < 100; ++i) bytes_a[i] = 0xAA;
    for (int i = 0; i < 200; ++i) bytes_b[i] = 0xBB;
    assert(bytes_a[99] == 0xAA && bytes_b[0] == 0xBB);
    assert(heap::consistent());

    // 先放中間，再放兩邊；最後一次釋放必須把三塊與尾端合併回一整塊。
    // Free the middle, then both sides; the last free has to merge all three
    // and the tail back into one block.
    heap::free(b);
    assert(heap::consistent());
    heap::free(a);
    assert(heap::consistent());
    heap::free(c);
    assert(heap::consistent());
    assert(heap::available() == heap::capacity());
    assert(heap::largest_available() == heap::capacity() - 16);

    // 釋放後的空間可以重新使用。 / Freed space is reused.
    void* again = heap::allocate(100);
    assert(again == a);
    heap::free(again);
}

void test_alignment() {
    heap::initialize(arena, sizeof(arena));
    void* small = heap::allocate(8);
    void* page = heap::allocate(4096, 4096);
    void* odd = heap::allocate(24, 64);
    assert(small != nullptr && page != nullptr && odd != nullptr);
    assert(aligned(page, 4096) && aligned(odd, 64));
    assert(heap::consistent());
    heap::free(page);
    heap::free(small);
    heap::free(odd);
    assert(heap::consistent() && heap::available() == heap::capacity());
}

void test_bad_frees_are_ignored() {
    heap::initialize(arena, sizeof(arena));
    void* block = heap::allocate(64);
    const auto before = heap::available();
    heap::free(nullptr);
    // 不在堆積裡、不在區塊開頭的指標都不算數。
    // A pointer outside the heap or not at a block's start does not count.
    int outside = 0;
    heap::free(&outside);
    heap::free(static_cast<unsigned char*>(block) + 16);
    assert(heap::available() == before && heap::consistent());
    heap::free(block);
    const auto after = heap::available();
    // 重複釋放被忽略。 / A double free is ignored.
    heap::free(block);
    assert(heap::available() == after && heap::consistent());
}

void test_exhaustion() {
    heap::initialize(arena, sizeof(arena));
    void* blocks[128]{};
    std::size_t count = 0;
    while (count < 128 && (blocks[count] = heap::allocate(1000)) != nullptr) ++count;
    assert(count > 0 && count < 128);
    assert(heap::allocate(1000) == nullptr);
    assert(heap::consistent());
    // 隔一塊釋放一塊造成碎片：總量夠，但最大的一塊放不下兩倍大的請求。
    // Freeing every other block fragments the heap: the total is there, but no
    // single block holds a request twice the size.
    for (std::size_t i = 0; i < count; i += 2) heap::free(blocks[i]);
    assert(heap::consistent());
    assert(heap::available() >= 2000 && heap::allocate(2100) == nullptr);
    for (std::size_t i = 1; i < count; i += 2) heap::free(blocks[i]);
    assert(heap::consistent() && heap::available() == heap::capacity());
    assert(heap::allocate(2100) != nullptr);
}

} // namespace

int main() {
    test_empty_regions();
    test_allocate_and_merge();
    test_alignment();
    test_bad_frees_are_ignored();
    test_exhaustion();
    return 0;
}
