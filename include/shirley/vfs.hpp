#pragma once

#include "shirley/device.hpp"
#include "shirley/io.hpp"

#include <cstddef>
#include <cstdint>

// 虛擬檔案系統。上層只認得路徑與描述子，不知道路徑的另一端是核心裡的
// SHRFS 映像、一個裝置，還是之後掛上來的別的東西：
//
//     shell / ELF loader
//            ↓
//           vfs           路徑解析、掛載表、開啟的檔案
//         ↙     ↘
//     shrfs     devfs
//       ↓         ↓
//   BlockDevice  device::Device
//
// `/dev` 是一個命名空間，不是驅動程式。devfs 不持有任何裝置狀態，它只是把
// 裝置註冊表當成目錄呈現出來，因此 `/dev/kbd0` 與 `device::find("kbd0")` 指
// 的是同一個物件。
//
// The virtual file system. Everything above it knows paths and descriptors and
// never whether the far end is the SHRFS image inside the kernel, a device, or
// something mounted later:
//
//     shell / ELF loader
//            ↓
//           vfs           path resolution, the mount table, open files
//         ↙     ↘
//     shrfs     devfs
//       ↓         ↓
//   BlockDevice  device::Device
//
// `/dev` is a namespace, not a driver. devfs holds no device state of its own;
// it presents the device registry as a directory, so `/dev/kbd0` and
// `device::find("kbd0")` name the very same object.
namespace shirley::vfs {

// 路徑與名稱長度上限，與 SHRFS 的限制一致。
// The path and name limits, the same ones SHRFS uses.
constexpr std::size_t max_path_length = 128;
constexpr std::size_t max_name_length = 55;

// 節點種類。目錄與檔案來自檔案系統；兩種裝置來自 devfs，分開是因為只有區塊
// 裝置能做區塊層存取。
//
// What a node is. Directories and files come from a file system; the two
// device kinds come from devfs, and they are separate because only a block
// device can be addressed in blocks.
enum class Type : std::uint8_t { File, Directory, CharacterDevice, BlockDevice };
const char* type_name(Type type);

class FileSystem;

// 一個已解析的節點。`path` 是正規化後的絕對路徑，之所以帶著它，是因為
// 掛載點的存在讓「這個目錄底下有什麼」不再只由一個檔案系統回答：列出 `/`
// 時必須把掛在 `/dev` 的 devfs 也算進去，而那需要知道自己是誰。
//
// One resolved node. `path` is the normalized absolute path, and it is carried
// because mount points mean "what is in this directory" is no longer answered
// by one file system alone: listing `/` has to include the devfs mounted at
// `/dev`, and that takes knowing which directory this is.
struct Node {
    FileSystem* filesystem = nullptr;
    // 檔案系統私有的識別碼；SHRFS 放的是目錄表索引。
    // A file-system-private identifier; SHRFS puts its entry index here.
    std::uint64_t id = 0;
    // 這個節點代表的裝置，只有 devfs 的節點會設定。VFS 需要它才能把區塊層
    // 的存取交給裝置，因此它放在節點裡而不是藏在檔案系統私有欄位後面。
    //
    // The device this node names, set only by devfs nodes. The VFS needs it to
    // hand block-level access to the device, so it sits in the node rather
    // than behind a file-system-private field.
    device::Device* device = nullptr;
    Type type = Type::File;
    std::uint64_t size = 0;
    char name[max_name_length + 1]{};
    char path[max_path_length]{};

    bool directory() const { return type == Type::Directory; }
};

// 一個掛載上來的檔案系統。SHRFS 與 devfs 都實作這個介面，之後可寫入的檔案
// 系統也一樣；VFS 本身不知道任何一種格式。
//
// A mounted file system. SHRFS and devfs both implement this interface, and so
// will a writable file system later; the VFS itself knows no format at all.
class FileSystem {
public:
    virtual const char* name() const = 0;
    // 這個檔案系統的根目錄。`node.path` 由 VFS 依掛載點填上，因此這裡不必
    // 也不應該去猜自己被掛在哪裡。
    //
    // This file system's root. `node.path` is filled in by the VFS from the
    // mount point, so an implementation neither needs nor should guess where
    // it was mounted.
    virtual bool root(Node& node) = 0;
    // 在 directory 底下找一個名字；`name` 不含斜線，也不會是 "." 或 ".."，
    // 那兩個在路徑正規化時就處理掉了。
    //
    // Find one name inside directory. `name` contains no slash and is never
    // "." or "..", both of which path normalization has already resolved.
    virtual bool lookup(const Node& directory, const char* name, Node& result) = 0;
    // 依序取出目錄的第 position 個項目；沒有更多項目時回傳 false。
    // Take the directory's entry at position; returns false once there are no
    // more.
    virtual bool list(const Node& directory, std::size_t position, Node& child) = 0;
    virtual io::Result read(const Node& file, std::uint64_t offset, void* buffer,
                            std::size_t length) = 0;
    // 唯讀的檔案系統回傳 Unsupported。
    // A read-only file system returns Unsupported.
    virtual io::Result write(const Node& file, std::uint64_t offset, const void* buffer,
                             std::size_t length) = 0;
    // 這個節點寫得進去嗎。open() 靠它在開啟時就拒絕寫入，而不是等到第一次
    // 寫入才失敗——呼叫端往往已經在那時候丟掉了原本的資料。以節點為單位而不是
    // 以檔案系統為單位，因為 devfs 底下的 `null` 寫得進去，`kbd0` 寫不進去。
    //
    // Whether this node can be written. open() uses it to refuse a write at
    // open time rather than at the first write, by which point a caller has
    // often already thrown away what it meant to save. It is per node rather
    // than per file system because under devfs `null` can be written and
    // `kbd0` cannot.
    virtual bool writable(const Node& node) const = 0;

protected:
    ~FileSystem() = default;
};

// 同時可以掛載的檔案系統數，以及同時可以開啟的檔案數。兩者都是固定大小的表：
// 核心沒有動態配置器，而目前的用量離上限還很遠。
//
// How many file systems can be mounted and how many files can be open at once.
// Both are fixed tables: the kernel has no dynamic allocator, and today's usage
// is nowhere near either limit.
constexpr std::size_t max_mounts = 4;
constexpr std::size_t max_open_files = 16;

// 開啟時要求的存取權。要求寫入而檔案系統唯讀時 open() 就會失敗，而不是等到
// 第一次寫入才發現。
//
// The access an open asks for. Asking for write on a read-only file system
// fails at open() rather than at the first write.
enum class OpenFlags : std::uint8_t { Read = 1u << 0, Write = 1u << 1 };
constexpr OpenFlags operator|(OpenFlags left, OpenFlags right) {
    return static_cast<OpenFlags>(static_cast<std::uint8_t>(left) |
                                  static_cast<std::uint8_t>(right));
}
constexpr bool contains(OpenFlags value, OpenFlags flag) {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

// open() 的失敗原因。描述子是非負整數，因此失敗以負數表示，而且分得出是哪
// 一種：shell 要能說出「沒有這個檔案」與「那是一個目錄」的差別。
//
// Why an open failed. A descriptor is a non-negative integer, so a failure is
// negative and says which kind it was: a shell has to be able to tell "no such
// file" apart from "that is a directory".
constexpr int error_not_found = -1;
constexpr int error_is_directory = -2;
constexpr int error_read_only = -3;
constexpr int error_no_descriptors = -4;
constexpr int error_invalid = -5;
const char* error_text(int error);

// 清空掛載表與描述子表；必須在任何掛載之前呼叫。
// Clear the mount table and the descriptor table. Must run before any mount.
void initialize();

// 把檔案系統掛在一個絕對路徑上。第一個掛載必須是 "/"，否則之後的路徑解析
// 沒有起點。路徑重複、表滿或路徑不是絕對路徑時回傳 false。
//
// Mount a file system at an absolute path. The first mount has to be "/", or
// later path resolution has no place to start. A duplicate path, a full table,
// or a path that is not absolute returns false.
bool mount(const char* path, FileSystem& filesystem);
bool unmount(const char* path);
std::size_t mount_count();
// 依索引取得掛載點的路徑與檔案系統，供診斷使用。
// The mount point's path and file system by index, for diagnostics.
const char* mount_path(std::size_t index);
FileSystem* mount_filesystem(std::size_t index);
bool mounted();

// 把 path 正規化成絕對路徑。相對路徑相對於 base 解析（base 為空時相對於根
// 目錄），"." 與 ".." 在這裡處理掉，重複的斜線會被收斂。放不下時回傳 false
// 而不是截斷：截斷的路徑會安靜地指向另一個檔案。
//
// Normalize path into an absolute one. A relative path resolves against base,
// or against the root when base is null; "." and ".." are resolved here and
// repeated slashes collapse. Returns false rather than truncating when the
// result does not fit: a truncated path quietly names a different file.
bool normalize(const char* path, const char* base, char* buffer, std::size_t capacity);

// 解析路徑並取得節點；找不到時回傳 false。
// Resolve a path to a node; returns false when there is nothing there.
bool stat(const char* path, Node& node, const char* base = nullptr);

// 列出目錄的第 position 個項目。掛在這個目錄底下的檔案系統會接在該目錄自己
// 的項目之後出現，因此 `ls /` 看得到 `dev`，即使根檔案系統裡並沒有這個項目。
//
// Take the directory's entry at position. A file system mounted inside this
// directory appears after the directory's own entries, which is why `ls /`
// shows `dev` even though the root file system has no such entry.
bool list(const Node& directory, std::size_t position, Node& child);
std::size_t child_count(const Node& directory);

// 開啟路徑，回傳描述子或上面那組負數錯誤碼。目錄不能被開啟成檔案。
// Open a path and return a descriptor, or one of the negative errors above. A
// directory cannot be opened as a file.
int open(const char* path, OpenFlags flags = OpenFlags::Read, const char* base = nullptr);
bool close(int descriptor);
// 描述子對應的節點；描述子無效時回傳 nullptr。
// The node a descriptor names, or nullptr when the descriptor is not valid.
const Node* node_of(int descriptor);

// 從目前位置讀寫，並前進該位置。讀到檔案結尾回傳 0 個位元組而不是錯誤。
// Read and write at the current position, advancing it. Reaching the end of a
// file yields zero bytes rather than an error.
io::Result read(int descriptor, void* buffer, std::size_t length);
io::Result write(int descriptor, const void* buffer, std::size_t length);
// 設定與查詢檔案位置。字元裝置沒有位置的概念，seek 對它們沒有作用。
// Set and query the file position. A character device has no position, and
// seeking one does nothing.
bool seek(int descriptor, std::uint64_t offset);
std::uint64_t position(int descriptor);

// 區塊層存取。只有區塊裝置支援，其餘一律回傳 Unsupported；這條路徑不經過
// 檔案位置，磁區編號是呼叫端自己給的。
//
// Block-level access. Only a block device supports it and everything else
// reports Unsupported. This path ignores the file position: the caller names
// the sectors itself.
io::Result block_read(int descriptor, std::uint64_t first, std::size_t count, void* buffer);
io::Result block_write(int descriptor, std::uint64_t first, std::size_t count,
                       const void* buffer);
std::size_t block_size(int descriptor);
std::uint64_t block_count(int descriptor);

// 把整個檔案讀進緩衝區，回傳讀到的位元組數；檔案放不下或不存在時回傳 0。
// ELF loader 需要一段連續的映像才能走 program header，因此這個便利函式存在。
//
// Read a whole file into a buffer and return how many bytes it holds; a file
// that does not exist or does not fit returns 0. The ELF loader needs one
// contiguous image to walk the program headers, which is why this exists.
std::size_t read_file(const char* path, void* buffer, std::size_t capacity);

// devfs 的實例。它沒有自己的狀態，內容就是裝置註冊表。
// The devfs instance. It has no state of its own; its content is the device
// registry.
FileSystem& devfs();

} // namespace shirley::vfs
