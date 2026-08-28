#pragma once

#include "shirley/block_device.hpp"

#include <cstddef>
#include <cstdint>

// SHRFS1：唯讀的扁平檔案系統。目錄不儲存子項目清單，而是每個項目記錄自己
// 所屬的目錄索引，因此列出目錄就是掃過目錄表挑出父項目相符的項目。在這個
// 規模下掃描的成本可以忽略，換來的是清單不可能和項目本身不一致。
//
// 所有存取都經過 io::BlockDevice，所以同一份程式碼之後接上真正的磁碟驅動
// 程式也能運作，不必知道背後是記憶體還是磁碟。
//
// SHRFS1: a read-only flat file system. A directory does not store a list of
// its children; instead every entry records the directory it belongs to, so
// listing a directory means scanning the table for entries whose parent
// matches. At this size the scan costs nothing, and in exchange a listing can
// never disagree with the entries it names.
//
// Every access goes through io::BlockDevice, so the same code works over a
// real disk driver later without knowing whether memory or a disk is behind
// it.
namespace shirley::fs {

// 名稱欄位為 56 位元組，扣掉結尾的 null 之後可以放 55 個字元。
// The name field is 56 bytes, which leaves 55 characters once the terminator
// is accounted for.
constexpr std::size_t max_name_length = 55;
// 絕對路徑的上限，同時也是 shell 工作目錄字串的大小。
// The longest absolute path, which is also the size of the shell's working
// directory string.
constexpr std::size_t max_path_length = 128;
// 目錄巢狀深度上限；path_of() 需要先收集途中的每一層才能反向組出路徑。
// The deepest nesting allowed. path_of() has to collect every level on the way
// up before it can assemble the path in the right order.
constexpr std::size_t max_depth = 16;

// 一個檔案或目錄。index 是它在目錄表中的位置，parent 則是所屬目錄的 index；
// 根目錄的 index 與 parent 都是 0，因此往上走一定會停下來。
//
// One file or directory. index is its slot in the entry table and parent is
// the slot of the directory holding it. The root has index and parent both 0,
// so walking upwards always terminates.
struct Node {
    char name[max_name_length + 1]{};
    bool directory = false;
    std::uint64_t size = 0;
    std::uint32_t index = 0;
    std::uint32_t parent = 0;
};

// 掛載區塊裝置上的映像。標頭不合法、版本不認得，或目錄表超出裝置範圍時
// 回傳 false，並且維持未掛載狀態。
//
// Mount the image on a block device. Returns false and stays unmounted when
// the header is invalid, the version is not recognized, or the entry table
// does not fit inside the device.
bool mount(io::BlockDevice& device);
void unmount();
bool mounted();

std::size_t entry_count();
// 所有檔案內容的總位元組數，供開機診斷輸出使用。
// The total size of every file's contents, for boot diagnostics.
std::uint64_t total_file_bytes();

bool root(Node& node);
bool entry(std::uint32_t index, Node& node);

// 解析路徑。以 '/' 開頭的路徑從根目錄開始，其餘從 base 開始（base 為空時
// 同樣從根目錄開始）。"." 與 ".." 在走訪過程中處理，不先改寫字串。
//
// Resolve a path. One starting with '/' is walked from the root and any other
// from base, which defaults to the root when null. "." and ".." are handled
// while walking rather than by rewriting the string first.
bool lookup(const char* path, Node& node, const Node* base = nullptr);

// 依序取出目錄的第 position 個項目；沒有更多項目時回傳 false。
// Take the directory's entry at position; returns false once there are no more.
bool list(const Node& directory, std::size_t position, Node& child);
std::size_t child_count(const Node& directory);

// 從檔案的 offset 讀出最多 length 個位元組。讀到檔案結尾會回傳較少的位元組
// 而不是錯誤；node 是目錄時回傳 InvalidArgument。
//
// Read up to length bytes from offset within a file. Reaching the end returns
// fewer bytes rather than an error; a directory returns InvalidArgument.
io::Result read(const Node& file, std::uint64_t offset, void* buffer, std::size_t length);

// 組出項目的絕對路徑。緩衝區不足或巢狀過深時回傳 false。
// Assemble an entry's absolute path. Returns false when the buffer is too
// small or the tree is nested deeper than max_depth.
bool path_of(const Node& node, char* buffer, std::size_t capacity);

} // namespace shirley::fs
