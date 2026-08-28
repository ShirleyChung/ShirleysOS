#include "shirley/fs.hpp"

#include "shirley/text.hpp"

namespace shirley::fs {
namespace {

// 映像的固定尺寸。標頭與項目都以小端序逐位元組解碼，因此核心的結構排列
// 方式（padding、對齊）完全不影響能不能讀懂映像。
//
// The image's fixed sizes. Both the header and the entries are decoded byte by
// byte as little-endian, so how the kernel happens to lay out a struct —
// padding and alignment included — cannot affect whether the image is
// readable.
constexpr std::size_t header_bytes = 32;
constexpr std::size_t entry_bytes = 80;
constexpr std::size_t name_bytes = 56;
constexpr std::uint32_t directory_flag = 1;
constexpr std::uint32_t supported_version = 1;
constexpr char magic[] = {'S', 'H', 'R', 'F', 'S', '1', '\0', '\0'};

// 一個區塊的緩衝區。檔案系統只透過區塊裝置存取資料，因此跨越區塊邊界的
// 讀取都在這裡拼起來。
//
// One block's worth of buffer. The file system only ever reaches its data
// through the block device, so a read spanning a block boundary is stitched
// together here.
constexpr std::size_t bounce_bytes = 512;
std::uint8_t bounce[bounce_bytes];

io::BlockDevice* device = nullptr;
std::uint32_t entries = 0;
std::uint64_t table_offset = 0;
std::uint64_t data_offset = 0;
std::uint64_t image_bytes = 0;
std::uint64_t file_bytes = 0;

std::uint32_t load_u32(const std::uint8_t* bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8 |
           static_cast<std::uint32_t>(bytes[2]) << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
}

std::uint64_t load_u64(const std::uint8_t* bytes) {
    return static_cast<std::uint64_t>(load_u32(bytes)) |
           static_cast<std::uint64_t>(load_u32(bytes + 4)) << 32;
}

// 從裝置讀出任意位移、任意長度的位元組。位移落在區塊中間或跨越多個區塊都
// 由這裡處理，上層完全不必知道區塊大小。
//
// Read an arbitrary byte range from the device. An offset inside a block or a
// range spanning several of them is handled here, so nothing above needs to
// know the block size at all.
bool read_bytes(std::uint64_t offset, void* buffer, std::size_t length) {
    if (device == nullptr) return false;
    if (length != 0 && buffer == nullptr) return false;
    const auto block = device->block_size();
    if (block == 0 || block > bounce_bytes) return false;
    auto* out = static_cast<std::uint8_t*>(buffer);
    while (length != 0) {
        const std::uint64_t index = offset / block;
        const auto within = static_cast<std::size_t>(offset % block);
        if (index >= device->block_count()) return false;
        if (!device->read_blocks(index, 1, bounce)) return false;
        std::size_t amount = block - within;
        if (amount > length) amount = length;
        for (std::size_t i = 0; i < amount; ++i) out[i] = bounce[within + i];
        out += amount;
        offset += amount;
        length -= amount;
    }
    return true;
}

bool read_entry(std::uint32_t index, Node& node) {
    if (device == nullptr || index >= entries) return false;
    std::uint8_t raw[entry_bytes];
    if (!read_bytes(table_offset + static_cast<std::uint64_t>(index) * entry_bytes, raw, entry_bytes))
        return false;
    // 名稱欄位必須自己以 null 結尾，否則損壞的映像會讓後面的讀取衝出欄位。
    // The name field has to terminate itself, or a corrupt image would let a
    // later read run off the end of it.
    std::size_t name_length = 0;
    while (name_length < name_bytes && raw[name_length] != '\0') ++name_length;
    if (name_length > max_name_length) return false;
    for (std::size_t i = 0; i < name_length; ++i) node.name[i] = static_cast<char>(raw[i]);
    node.name[name_length] = '\0';
    const auto flags = load_u32(raw + name_bytes);
    node.directory = (flags & directory_flag) != 0;
    node.parent = load_u32(raw + name_bytes + 4);
    node.index = index;
    node.size = load_u64(raw + name_bytes + 16);
    if (node.parent >= entries) return false;
    // 檔案的資料必須完整落在映像內；越界的項目一律視為映像損壞。
    // A file's data has to lie entirely inside the image; an entry reaching
    // past it means the image is corrupt.
    const auto offset = load_u64(raw + name_bytes + 8);
    if (!node.directory) {
        if (offset < data_offset || offset > image_bytes || node.size > image_bytes - offset)
            return false;
    }
    return true;
}

std::uint64_t data_offset_of(std::uint32_t index) {
    std::uint8_t raw[entry_bytes];
    if (!read_bytes(table_offset + static_cast<std::uint64_t>(index) * entry_bytes, raw, entry_bytes))
        return 0;
    return load_u64(raw + name_bytes + 8);
}

// 取出路徑中的下一段名稱，並把 cursor 推到後面。連續的斜線會被跳過，因此
// "/etc//motd" 和 "/etc/motd" 指向同一個檔案。
//
// Take the next name from a path and advance the cursor past it. Repeated
// slashes are skipped, so "/etc//motd" and "/etc/motd" name the same file.
bool next_component(const char*& cursor, char* buffer, std::size_t capacity) {
    while (*cursor == '/') ++cursor;
    if (*cursor == '\0') return false;
    std::size_t length = 0;
    while (cursor[length] != '\0' && cursor[length] != '/') ++length;
    if (length + 1 > capacity) {
        // 名稱長到放不進緩衝區時，回報成找不到而不是截斷後亂比對。
        // A name too long for the buffer is reported as not found rather than
        // truncated and then matched against something else.
        cursor += length;
        return false;
    }
    for (std::size_t i = 0; i < length; ++i) buffer[i] = cursor[i];
    buffer[length] = '\0';
    cursor += length;
    return true;
}

} // namespace

bool mount(io::BlockDevice& block_device) {
    unmount();
    device = &block_device;

    std::uint8_t raw[header_bytes];
    if (!read_bytes(0, raw, header_bytes)) {
        unmount();
        return false;
    }
    for (std::size_t i = 0; i < sizeof(magic); ++i) {
        if (static_cast<char>(raw[i]) != magic[i]) {
            unmount();
            return false;
        }
    }
    const auto version = load_u32(raw + 8);
    const auto count = load_u32(raw + 12);
    const auto table = load_u32(raw + 16);
    const auto data = load_u32(raw + 20);
    const auto size = load_u64(raw + 24);
    const auto device_bytes = device->block_count() * device->block_size();
    // 目錄表與資料區都必須完整落在裝置裡，掛載之後才不需要再檢查一次。
    // Both the entry table and the data region must lie entirely inside the
    // device, which is what makes it safe to stop rechecking after mounting.
    if (version != supported_version || count == 0 || table < header_bytes || data < table ||
        size > device_bytes ||
        static_cast<std::uint64_t>(count) * entry_bytes > size - table ||
        table + static_cast<std::uint64_t>(count) * entry_bytes > data) {
        unmount();
        return false;
    }

    entries = count;
    table_offset = table;
    data_offset = data;
    image_bytes = size;

    // 根目錄必須真的是目錄，而且每個項目都要讀得起來：與其讓第一個 ls 才
    // 發現映像壞掉，不如在掛載時就失敗。
    //
    // The root has to really be a directory and every entry has to decode, so
    // a broken image fails at mount time instead of surprising the first ls.
    Node node;
    if (!read_entry(0, node) || !node.directory) {
        unmount();
        return false;
    }
    file_bytes = 0;
    for (std::uint32_t index = 0; index < entries; ++index) {
        if (!read_entry(index, node)) {
            unmount();
            return false;
        }
        if (!node.directory) file_bytes += node.size;
    }
    return true;
}

void unmount() {
    device = nullptr;
    entries = 0;
    table_offset = 0;
    data_offset = 0;
    image_bytes = 0;
    file_bytes = 0;
}

bool mounted() { return device != nullptr && entries != 0; }
std::size_t entry_count() { return entries; }
std::uint64_t total_file_bytes() { return file_bytes; }

bool root(Node& node) { return entry(0, node); }

bool entry(std::uint32_t index, Node& node) {
    if (!mounted()) return false;
    return read_entry(index, node);
}

bool lookup(const char* path, Node& node, const Node* base) {
    if (!mounted() || path == nullptr) return false;
    Node current;
    if (path[0] == '/' || base == nullptr) {
        if (!root(current)) return false;
    } else {
        current = *base;
    }

    char component[max_name_length + 1];
    const char* cursor = path;
    while (true) {
        while (*cursor == '/') ++cursor;
        if (*cursor == '\0') break;
        if (!next_component(cursor, component, sizeof(component))) return false;
        if (text::equals(component, ".")) continue;
        if (text::equals(component, "..")) {
            if (!entry(current.parent, current)) return false;
            continue;
        }
        // 只有目錄底下才找得到東西；把檔案當成目錄往下走是錯誤。
        // Only a directory can contain anything, so walking into a file is an
        // error rather than an empty result.
        if (!current.directory) return false;
        bool found = false;
        for (std::uint32_t index = 1; index < entries; ++index) {
            Node candidate;
            if (!read_entry(index, candidate)) return false;
            if (candidate.parent != current.index) continue;
            if (!text::equals(candidate.name, component)) continue;
            current = candidate;
            found = true;
            break;
        }
        if (!found) return false;
    }
    node = current;
    return true;
}

bool list(const Node& directory, std::size_t position, Node& child) {
    if (!mounted() || !directory.directory) return false;
    std::size_t seen = 0;
    // 索引 0 是根目錄，而根目錄的父項目是自己；跳過它，列出根目錄時才不會
    // 把根目錄本身也列進去。
    //
    // Index 0 is the root and the root is its own parent. Skipping it is what
    // keeps a listing of the root from including the root itself.
    for (std::uint32_t index = 1; index < entries; ++index) {
        Node candidate;
        if (!read_entry(index, candidate)) return false;
        if (candidate.parent != directory.index) continue;
        if (seen == position) {
            child = candidate;
            return true;
        }
        ++seen;
    }
    return false;
}

std::size_t child_count(const Node& directory) {
    if (!mounted() || !directory.directory) return 0;
    std::size_t count = 0;
    for (std::uint32_t index = 1; index < entries; ++index) {
        Node candidate;
        if (!read_entry(index, candidate)) return count;
        if (candidate.parent == directory.index) ++count;
    }
    return count;
}

io::Result read(const Node& file, std::uint64_t offset, void* buffer, std::size_t length) {
    if (!mounted()) return {0, io::Error::Unsupported};
    if (file.directory) return {0, io::Error::InvalidArgument};
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (file.index >= entries) return {0, io::Error::InvalidArgument};
    if (offset >= file.size) return {0, io::Error::None};
    const auto remaining = file.size - offset;
    if (length > remaining) length = static_cast<std::size_t>(remaining);
    const auto start = data_offset_of(file.index);
    if (start == 0) return {0, io::Error::DeviceError};
    if (!read_bytes(start + offset, buffer, length)) return {0, io::Error::DeviceError};
    return {length, io::Error::None};
}

bool path_of(const Node& node, char* buffer, std::size_t capacity) {
    if (!mounted() || buffer == nullptr || capacity == 0) return false;
    if (!text::copy(buffer, capacity, "/")) return false;
    if (node.index == 0) return true;

    // 項目只知道自己的父項目，所以路徑要先由下往上收集，再反過來組出來。
    // An entry only knows its parent, so the path is collected upwards first
    // and assembled in the opposite order afterwards.
    std::uint32_t chain[max_depth];
    std::size_t depth = 0;
    Node current = node;
    while (current.index != 0) {
        if (depth == max_depth) return false;
        chain[depth++] = current.index;
        if (!entry(current.parent, current)) return false;
    }
    for (std::size_t i = depth; i > 0; --i) {
        Node step;
        if (!entry(chain[i - 1], step)) return false;
        if (!text::append(buffer, capacity, step.name)) return false;
        if (i > 1 && !text::append(buffer, capacity, '/')) return false;
    }
    return true;
}

} // namespace shirley::fs
