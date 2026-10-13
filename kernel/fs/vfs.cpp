#include "shirley/vfs.hpp"

#include "shirley/text.hpp"

namespace shirley::vfs {
namespace {

struct Mount {
    char path[max_path_length]{};
    FileSystem* filesystem = nullptr;
};

struct OpenFile {
    bool used = false;
    Node node;
    std::uint64_t position = 0;
    OpenFlags flags = OpenFlags::Read;
};

Mount mounts[max_mounts];
std::size_t mount_total = 0;
OpenFile files[max_open_files];

// 路徑中一個元件的長度上限，順便當成 normalize() 的暫存大小。
// The longest single path component, which is also the scratch size
// normalize() uses.
constexpr std::size_t max_component = max_name_length + 1;

bool is_separator(char value) { return value == '/'; }

// 把 buffer 末端的最後一個元件砍掉，也就是往上一層。已經在根目錄時什麼都不
// 做：根目錄的上一層還是根目錄，這樣走訪一定會停下來。
//
// Drop the last component from buffer, which is what going up one level means.
// At the root it does nothing: the root's parent is the root, and that is what
// makes walking upwards terminate.
void remove_last_component(char* buffer) {
    std::size_t length = text::length(buffer);
    while (length > 1 && !is_separator(buffer[length - 1])) --length;
    // 砍掉分隔符本身，除非剩下的就是根目錄那一個斜線。
    // Remove the separator too, unless what is left is the root's own slash.
    if (length > 1) --length;
    buffer[length] = '\0';
}

bool append_component(char* buffer, std::size_t capacity, const char* component) {
    const bool at_root = text::length(buffer) == 1;
    if (!at_root && !text::append(buffer, capacity, '/')) return false;
    return text::append(buffer, capacity, component);
}

// path 是否以 prefix 為「路徑前綴」。字串前綴不夠：`/devices` 不在掛在
// `/dev` 的檔案系統底下，兩者只是剛好共用前六個字元。
//
// Whether path lies under prefix as a path. A string prefix is not enough:
// `/devices` is not inside a file system mounted at `/dev`; the two merely
// share their first six characters.
bool path_starts_with(const char* path, const char* prefix) {
    const auto prefix_length = text::length(prefix);
    if (!text::equals(path, prefix, prefix_length)) return false;
    if (prefix_length == 1) return true; // "/" 之下就是全部 / everything is under "/"
    const char next = path[prefix_length];
    return next == '\0' || is_separator(next);
}

// 找出涵蓋這個路徑、掛載點最長的那個掛載。最長的才對：`/dev/kbd0` 同時位在
// `/` 與 `/dev` 之下，而應該回答它的是 devfs。
//
// Find the mount with the longest mount point covering this path. Longest is
// the right one: `/dev/kbd0` lies under both `/` and `/dev`, and the one that
// should answer for it is devfs.
Mount* mount_for(const char* path) {
    Mount* best = nullptr;
    std::size_t best_length = 0;
    for (std::size_t index = 0; index < mount_total; ++index) {
        auto& candidate = mounts[index];
        if (!path_starts_with(path, candidate.path)) continue;
        const auto length = text::length(candidate.path);
        if (best != nullptr && length <= best_length) continue;
        best = &candidate;
        best_length = length;
    }
    return best;
}

OpenFile* file_of(int descriptor) {
    if (descriptor < 0 || static_cast<std::size_t>(descriptor) >= max_open_files) return nullptr;
    auto& file = files[static_cast<std::size_t>(descriptor)];
    return file.used ? &file : nullptr;
}

// 從掛載點的根目錄開始，逐段走完路徑剩下的部分。
// Walk whatever the path has left, starting at the mount's root.
bool walk(Mount& mount, const char* path, Node& node) {
    Node current;
    if (!mount.filesystem->root(current)) return false;
    current.filesystem = mount.filesystem;
    if (!text::copy(current.path, sizeof(current.path), mount.path)) return false;

    const char* remainder = path + text::length(mount.path);
    while (*remainder != '\0') {
        while (is_separator(*remainder)) ++remainder;
        if (*remainder == '\0') break;
        char component[max_component];
        std::size_t length = 0;
        while (remainder[length] != '\0' && !is_separator(remainder[length])) {
            if (length + 1 >= sizeof(component)) return false;
            component[length] = remainder[length];
            ++length;
        }
        component[length] = '\0';
        remainder += length;

        // 只有目錄可以往下走；把檔案當成目錄走進去是錯誤，不是空目錄。
        // Only a directory can be descended into. Walking into a file is an
        // error rather than an empty directory.
        if (!current.directory()) return false;
        Node child;
        if (!mount.filesystem->lookup(current, component, child)) return false;
        child.filesystem = mount.filesystem;
        if (!text::copy(child.path, sizeof(child.path), current.path)) return false;
        if (!append_component(child.path, sizeof(child.path), component)) return false;
        current = child;
    }
    node = current;
    return true;
}

// 掛在這個目錄「正下方」的掛載點有幾個，以及第幾個是哪一個。掛在自己身上的
// 那個掛載不算：`/dev` 的內容是 devfs 的根目錄，不是 `/dev` 底下的一個項目。
//
// How many mount points sit directly inside this directory, and which one is
// which. A mount at the directory itself does not count: the content of `/dev`
// is devfs's root, not an entry inside `/dev`.
Mount* nested_mount(const Node& directory, std::size_t position) {
    std::size_t seen = 0;
    for (std::size_t index = 0; index < mount_total; ++index) {
        auto& candidate = mounts[index];
        if (text::equals(candidate.path, directory.path)) continue;
        if (!path_starts_with(candidate.path, directory.path)) continue;
        // 「正下方」表示掛載點去掉這個目錄的前綴之後只剩一個元件。
        // "Directly inside" means one component is left once this directory's
        // prefix is removed.
        const auto prefix = text::length(directory.path);
        const char* leaf = candidate.path + prefix;
        while (is_separator(*leaf)) ++leaf;
        bool nested = true;
        for (const char* scan = leaf; *scan != '\0'; ++scan) {
            if (is_separator(*scan)) nested = false;
        }
        if (!nested || *leaf == '\0') continue;
        if (seen++ != position) continue;
        return &candidate;
    }
    return nullptr;
}

// 掛載點在父目錄的清單裡，看起來就是一個目錄項目。
// A mount point looks like a directory entry in its parent's listing.
bool mount_as_child(Mount& mount, Node& child) {
    if (!mount.filesystem->root(child)) return false;
    child.filesystem = mount.filesystem;
    child.type = Type::Directory;
    if (!text::copy(child.path, sizeof(child.path), mount.path)) return false;
    const char* leaf = mount.path;
    for (const char* scan = mount.path; *scan != '\0'; ++scan) {
        if (is_separator(*scan)) leaf = scan + 1;
    }
    return text::copy(child.name, sizeof(child.name), leaf);
}

// 這個路徑是不是掛載點，或者底下有掛載點。這樣的目錄不能被刪除或搬走：
// 掛載點會變成指向不存在的地方。
//
// Whether this path is a mount point or has one beneath it. Such a directory
// cannot be deleted or moved: the mount point would be left naming nothing.
bool covers_mount(const char* path) {
    for (std::size_t index = 0; index < mount_total; ++index)
        if (path_starts_with(mounts[index].path, path)) return true;
    return false;
}

// 這個路徑本身或它底下有沒有開著的檔案。刪除或搬走它們，會讓那些描述子指向
// 一個已經不在那裡的節點。
//
// Whether a file at this path, or beneath it, is open. Deleting or moving it
// would leave those descriptors naming a node that is no longer there.
bool covers_open_file(const char* path) {
    for (std::size_t index = 0; index < max_open_files; ++index)
        if (files[index].used && path_starts_with(files[index].node.path, path)) return true;
    return false;
}

// 把路徑拆成父目錄與最後一個名稱，並解析父目錄。根目錄沒有父目錄。
// Split a path into its parent directory and final name, and resolve the
// parent. The root has no parent.
io::Error parent_of(const char* path, const char* base, char* normalized, Node& parent,
                    char* leaf) {
    if (path == nullptr) return io::Error::InvalidArgument;
    if (!normalize(path, base, normalized, max_path_length)) return io::Error::InvalidArgument;
    if (text::equals(normalized, "/")) return io::Error::InvalidArgument;
    const char* last = normalized;
    for (const char* scan = normalized; *scan != '\0'; ++scan)
        if (is_separator(*scan)) last = scan + 1;
    if (!text::copy(leaf, max_name_length + 1, last)) return io::Error::InvalidArgument;
    char parent_path[max_path_length];
    if (!text::copy(parent_path, sizeof(parent_path), normalized)) return io::Error::InvalidArgument;
    remove_last_component(parent_path);
    if (!stat(parent_path, parent)) return io::Error::NotFound;
    if (!parent.directory()) return io::Error::NotFound;
    return io::Error::None;
}

} // namespace

io::Error FileSystem::create(const Node&, const char*, Type, Node&) { return io::Error::Unsupported; }
io::Error FileSystem::remove(const Node&, const char*) { return io::Error::Unsupported; }
io::Error FileSystem::rename(const Node&, const char*, const Node&, const char*) {
    return io::Error::Unsupported;
}
io::Error FileSystem::truncate(const Node&, std::uint64_t) { return io::Error::Unsupported; }
io::Error FileSystem::sync() { return io::Error::None; }

const char* type_name(Type type) {
    switch (type) {
    case Type::File: return "file";
    case Type::Directory: return "dir";
    case Type::CharacterDevice: return "char";
    case Type::BlockDevice: return "block";
    }
    return "unknown";
}

const char* error_text(int error) {
    switch (error) {
    case error_not_found: return "no such file or directory";
    case error_is_directory: return "is a directory";
    case error_read_only: return "read-only file system";
    case error_no_descriptors: return "too many open files";
    case error_invalid: return "invalid argument";
    case error_no_space: return "no space left on device";
    }
    return error < 0 ? "unknown error" : "success";
}

void initialize() {
    for (std::size_t index = 0; index < max_mounts; ++index) {
        mounts[index].path[0] = '\0';
        mounts[index].filesystem = nullptr;
    }
    mount_total = 0;
    for (std::size_t index = 0; index < max_open_files; ++index) files[index] = OpenFile{};
}

bool mount(const char* path, FileSystem& filesystem) {
    if (path == nullptr || path[0] != '/') return false;
    if (mount_total >= max_mounts) return false;
    // 第一個掛載必須是根目錄，否則之後每一次路徑解析都沒有起點。
    // The first mount has to be the root, or every later resolution has
    // nowhere to start.
    if (mount_total == 0 && !text::equals(path, "/")) return false;
    char normalized[max_path_length];
    if (!normalize(path, nullptr, normalized, sizeof(normalized))) return false;
    for (std::size_t index = 0; index < mount_total; ++index) {
        if (text::equals(mounts[index].path, normalized)) return false;
    }
    // 掛載點本身不需要事先存在——列出目錄時會把掛載點當成一個項目補上去，因此
    // `ls /` 看得到 `dev`，即使根檔案系統裡沒有這個目錄。要在唯讀映像裡放一個
    // 空目錄，只是為了讓它馬上被蓋掉，沒有任何意義。
    //
    // 但父目錄必須存在：掛在一個不存在的目錄底下，那個掛載除了用完整路徑之外
    // 沒有任何方法能走到。根目錄沒有父目錄，因此不適用。
    //
    // The mount point itself need not exist beforehand: a listing synthesizes
    // it as an entry, which is why `ls /` shows `dev` even though the root file
    // system has no such directory. Putting an empty directory into a read-only
    // image purely to be covered up immediately would serve nothing.
    //
    // The parent does have to exist, though: a mount under a directory that is
    // not there could never be reached except by naming its full path. The root
    // has no parent, so the rule does not apply to it.
    if (mount_total != 0) {
        char parent[max_path_length];
        if (!text::copy(parent, sizeof(parent), normalized)) return false;
        remove_last_component(parent);
        Node point;
        if (!stat(parent, point) || !point.directory()) return false;
    }
    auto& entry = mounts[mount_total];
    if (!text::copy(entry.path, sizeof(entry.path), normalized)) return false;
    entry.filesystem = &filesystem;
    ++mount_total;
    return true;
}

bool unmount(const char* path) {
    if (path == nullptr) return false;
    char normalized[max_path_length];
    if (!normalize(path, nullptr, normalized, sizeof(normalized))) return false;
    for (std::size_t index = 0; index < mount_total; ++index) {
        if (!text::equals(mounts[index].path, normalized)) continue;
        // 還有檔案開在這個檔案系統上就不能卸載：卸載之後那些描述子會指向
        // 一個沒有人維護的檔案系統。
        //
        // A file system with open files cannot be unmounted: those descriptors
        // would be left pointing at a file system nobody owns any more.
        for (std::size_t open = 0; open < max_open_files; ++open) {
            if (files[open].used && files[open].node.filesystem == mounts[index].filesystem)
                return false;
        }
        mounts[index] = mounts[mount_total - 1];
        mounts[mount_total - 1] = Mount{};
        --mount_total;
        return true;
    }
    return false;
}

std::size_t mount_count() { return mount_total; }
const char* mount_path(std::size_t index) {
    return index < mount_total ? mounts[index].path : nullptr;
}
FileSystem* mount_filesystem(std::size_t index) {
    return index < mount_total ? mounts[index].filesystem : nullptr;
}
bool mounted() { return mount_total != 0; }

bool normalize(const char* path, const char* base, char* buffer, std::size_t capacity) {
    if (path == nullptr || buffer == nullptr || capacity == 0) return false;
    // 絕對路徑忽略 base；相對路徑從 base 開始，base 沒有給就從根目錄開始。
    // An absolute path ignores base. A relative one starts at base, or at the
    // root when no base was given.
    if (path[0] == '/' || base == nullptr || base[0] == '\0') {
        if (!text::copy(buffer, capacity, "/")) return false;
    } else {
        if (!text::copy(buffer, capacity, base)) return false;
    }

    const char* cursor = path;
    while (*cursor != '\0') {
        while (is_separator(*cursor)) ++cursor;
        if (*cursor == '\0') break;
        char component[max_component];
        std::size_t length = 0;
        while (cursor[length] != '\0' && !is_separator(cursor[length])) {
            if (length + 1 >= sizeof(component)) return false;
            component[length] = cursor[length];
            ++length;
        }
        component[length] = '\0';
        cursor += length;

        if (text::equals(component, ".")) continue;
        if (text::equals(component, "..")) {
            remove_last_component(buffer);
            continue;
        }
        if (!append_component(buffer, capacity, component)) return false;
    }
    return true;
}

bool stat(const char* path, Node& node, const char* base) {
    if (path == nullptr) return false;
    char normalized[max_path_length];
    if (!normalize(path, base, normalized, sizeof(normalized))) return false;
    auto* mount = mount_for(normalized);
    if (mount == nullptr) return false;
    return walk(*mount, normalized, node);
}

bool list(const Node& directory, std::size_t position, Node& child) {
    if (!directory.directory() || directory.filesystem == nullptr) return false;
    if (directory.filesystem->list(directory, position, child)) {
        child.filesystem = directory.filesystem;
        if (!text::copy(child.path, sizeof(child.path), directory.path)) return false;
        return append_component(child.path, sizeof(child.path), child.name);
    }
    // 檔案系統自己的項目用完之後，接著是掛在這個目錄底下的檔案系統。
    // Once the file system's own entries run out, the file systems mounted
    // inside this directory follow.
    std::size_t own = 0;
    Node scratch;
    while (directory.filesystem->list(directory, own, scratch)) ++own;
    if (position < own) return false;
    auto* nested = nested_mount(directory, position - own);
    if (nested == nullptr) return false;
    return mount_as_child(*nested, child);
}

std::size_t child_count(const Node& directory) {
    std::size_t total = 0;
    Node child;
    while (list(directory, total, child)) ++total;
    return total;
}

namespace {

int open_error(io::Error error) {
    switch (error) {
    case io::Error::NotFound: return error_not_found;
    case io::Error::ReadOnly: return error_read_only;
    case io::Error::NoSpace: return error_no_space;
    case io::Error::Unsupported: return error_read_only;
    default: return error_invalid;
    }
}

} // namespace

int open(const char* path, OpenFlags flags, const char* base) {
    if (path == nullptr) return error_invalid;
    Node node;
    if (!stat(path, node, base)) {
        if (!contains(flags, OpenFlags::Create)) return error_not_found;
        // 不存在而且要求建立：在父目錄裡建一個空檔案。
        // Missing and asked to be created: make an empty file in the parent.
        char normalized[max_path_length];
        char leaf[max_name_length + 1];
        Node parent;
        const auto found = parent_of(path, base, normalized, parent, leaf);
        if (found != io::Error::None) return open_error(found);
        if (!parent.filesystem->writable(parent)) return error_read_only;
        const auto made = parent.filesystem->create(parent, leaf, Type::File, node);
        if (made != io::Error::None) return open_error(made);
        node.filesystem = parent.filesystem;
        if (!text::copy(node.path, sizeof(node.path), normalized)) return error_invalid;
    }
    if (node.directory()) return error_is_directory;
    if (contains(flags, OpenFlags::Write) && !node.filesystem->writable(node))
        return error_read_only;
    if (contains(flags, OpenFlags::Write) && contains(flags, OpenFlags::Truncate) && node.size != 0) {
        const auto cut = node.filesystem->truncate(node, 0);
        if (cut != io::Error::None) return open_error(cut);
        node.size = 0;
    }
    for (std::size_t index = 0; index < max_open_files; ++index) {
        if (files[index].used) continue;
        files[index] = OpenFile{true, node, 0, flags};
        return static_cast<int>(index);
    }
    return error_no_descriptors;
}

bool close(int descriptor) {
    auto* file = file_of(descriptor);
    if (file == nullptr) return false;
    *file = OpenFile{};
    return true;
}

const Node* node_of(int descriptor) {
    auto* file = file_of(descriptor);
    return file == nullptr ? nullptr : &file->node;
}

io::Result read(int descriptor, void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    auto* file = file_of(descriptor);
    if (file == nullptr) return {0, io::Error::InvalidArgument};
    if (!contains(file->flags, OpenFlags::Read)) return {0, io::Error::Unsupported};
    const auto result = file->node.filesystem->read(file->node, file->position, buffer, length);
    if (result) file->position += result.transferred;
    return result;
}

io::Result write(int descriptor, const void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    auto* file = file_of(descriptor);
    if (file == nullptr) return {0, io::Error::InvalidArgument};
    if (!contains(file->flags, OpenFlags::Write)) return {0, io::Error::Unsupported};
    const auto result = file->node.filesystem->write(file->node, file->position, buffer, length);
    if (result) file->position += result.transferred;
    // 寫過檔案結尾就把記住的大小跟著放大，之後的 stat 與讀取才看得到。
    // Writing past the end grows the remembered size, so later reads and stat
    // see it.
    if (file->position > file->node.size && file->node.type == Type::File)
        file->node.size = file->position;
    return result;
}

bool seek(int descriptor, std::uint64_t offset) {
    auto* file = file_of(descriptor);
    if (file == nullptr) return false;
    file->position = offset;
    return true;
}

std::uint64_t position(int descriptor) {
    auto* file = file_of(descriptor);
    return file == nullptr ? 0 : file->position;
}

io::Error make_directory(const char* path, const char* base) {
    char normalized[max_path_length];
    char leaf[max_name_length + 1];
    Node parent;
    const auto found = parent_of(path, base, normalized, parent, leaf);
    if (found != io::Error::None) return found;
    Node existing;
    if (stat(normalized, existing)) return io::Error::AlreadyExists;
    if (!parent.filesystem->writable(parent)) return io::Error::ReadOnly;
    Node created;
    return parent.filesystem->create(parent, leaf, Type::Directory, created);
}

io::Error remove(const char* path, const char* base) {
    char normalized[max_path_length];
    char leaf[max_name_length + 1];
    Node parent;
    const auto found = parent_of(path, base, normalized, parent, leaf);
    if (found != io::Error::None) return found;
    Node node;
    if (!stat(normalized, node)) return io::Error::NotFound;
    if (covers_mount(normalized) || covers_open_file(normalized)) return io::Error::Busy;
    if (node.filesystem != parent.filesystem) return io::Error::Busy;
    if (!parent.filesystem->writable(parent)) return io::Error::ReadOnly;
    return parent.filesystem->remove(parent, leaf);
}

io::Error rename(const char* from, const char* to, const char* base) {
    char from_path[max_path_length];
    char from_leaf[max_name_length + 1];
    Node from_parent;
    auto found = parent_of(from, base, from_path, from_parent, from_leaf);
    if (found != io::Error::None) return found;
    char to_path[max_path_length];
    char to_leaf[max_name_length + 1];
    Node to_parent;
    found = parent_of(to, base, to_path, to_parent, to_leaf);
    if (found != io::Error::None) return found;

    Node node;
    if (!stat(from_path, node)) return io::Error::NotFound;
    if (text::equals(from_path, to_path)) return io::Error::None;
    Node existing;
    if (stat(to_path, existing)) return io::Error::AlreadyExists;
    if (covers_mount(from_path) || covers_open_file(from_path)) return io::Error::Busy;
    // 不同檔案系統之間搬移等於複製再刪除，那是 user 程式的事，不是 VFS 的。
    // A move between file systems is a copy and a delete, which is a user
    // program's business rather than the VFS's.
    if (node.filesystem != from_parent.filesystem || to_parent.filesystem != from_parent.filesystem)
        return io::Error::Unsupported;
    // 目錄不能搬進它自己底下，否則那棵子樹會跟根目錄斷開。
    // A directory cannot move inside itself, or that subtree would be cut off
    // from the root.
    if (path_starts_with(to_path, from_path)) return io::Error::InvalidArgument;
    if (!from_parent.filesystem->writable(from_parent)) return io::Error::ReadOnly;
    return from_parent.filesystem->rename(from_parent, from_leaf, to_parent, to_leaf);
}

io::Error truncate(int descriptor, std::uint64_t size) {
    auto* file = file_of(descriptor);
    if (file == nullptr) return io::Error::InvalidArgument;
    if (!contains(file->flags, OpenFlags::Write)) return io::Error::Unsupported;
    if (file->node.type != Type::File) return io::Error::Unsupported;
    const auto result = file->node.filesystem->truncate(file->node, size);
    if (result == io::Error::None) file->node.size = size;
    return result;
}

io::Error sync() {
    io::Error first = io::Error::None;
    for (std::size_t index = 0; index < mount_total; ++index) {
        const auto result = mounts[index].filesystem->sync();
        if (first == io::Error::None) first = result;
    }
    return first;
}

io::Result block_read(int descriptor, std::uint64_t first, std::size_t count, void* buffer) {
    auto* file = file_of(descriptor);
    if (file == nullptr) return {0, io::Error::InvalidArgument};
    if (!contains(file->flags, OpenFlags::Read)) return {0, io::Error::Unsupported};
    if (file->node.device == nullptr) return {0, io::Error::Unsupported};
    return file->node.device->block_read(first, count, buffer);
}

io::Result block_write(int descriptor, std::uint64_t first, std::size_t count,
                       const void* buffer) {
    auto* file = file_of(descriptor);
    if (file == nullptr) return {0, io::Error::InvalidArgument};
    if (!contains(file->flags, OpenFlags::Write)) return {0, io::Error::Unsupported};
    if (file->node.device == nullptr) return {0, io::Error::Unsupported};
    return file->node.device->block_write(first, count, buffer);
}

std::size_t block_size(int descriptor) {
    auto* file = file_of(descriptor);
    if (file == nullptr || file->node.device == nullptr) return 0;
    return file->node.device->block_size();
}

std::uint64_t block_count(int descriptor) {
    auto* file = file_of(descriptor);
    if (file == nullptr || file->node.device == nullptr) return 0;
    return file->node.device->block_count();
}

std::size_t read_file(const char* path, void* buffer, std::size_t capacity) {
    if (buffer == nullptr || capacity == 0) return 0;
    const auto descriptor = open(path);
    if (descriptor < 0) return 0;
    auto* bytes = static_cast<unsigned char*>(buffer);
    std::size_t total = 0;
    while (total < capacity) {
        const auto result = read(descriptor, bytes + total, capacity - total);
        if (!result || result.transferred == 0) break;
        total += result.transferred;
    }
    // 緩衝區剛好被填滿時，檔案有可能還沒讀完。回傳一份被截斷的映像比回報
    // 失敗危險得多——ELF loader 會拿它去走 program header。
    //
    // A buffer filled to the brim may mean the file was not finished. Handing
    // back a truncated image is far more dangerous than reporting failure: the
    // ELF loader would walk its program headers.
    if (total == capacity) {
        unsigned char extra = 0;
        const auto more = read(descriptor, &extra, 1);
        if (more && more.transferred != 0) total = 0;
    }
    close(descriptor);
    return total;
}

} // namespace shirley::vfs
