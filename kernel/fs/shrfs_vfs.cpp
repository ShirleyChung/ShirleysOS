#include "shirley/fs.hpp"
#include "shirley/text.hpp"
#include "shirley/vfs.hpp"

// 把既有的 SHRFS 掛到 VFS 上。這個檔案不碰映像格式：目錄表怎麼走、資料區在
// 哪裡，全部仍然是 kernel/fs/shrfs.cpp 的事，這裡只負責把它的 Node 換成 VFS
// 的 Node，並把「一個名字」的查詢換成 SHRFS 的逐項掃描。
//
// Mount the existing SHRFS on the VFS. This file touches no image format: how
// the entry table is walked and where the data region sits both remain
// kernel/fs/shrfs.cpp's business. All this does is turn its Node into the
// VFS's, and turn a lookup of one name into SHRFS's scan of the entries.
namespace shirley::fs {
namespace {

// SHRFS 的節點識別碼就是目錄表索引。
// A SHRFS node's identifier is its entry table index.
vfs::Node to_vfs(const Node& entry) {
    vfs::Node node;
    node.id = entry.index;
    node.type = entry.directory ? vfs::Type::Directory : vfs::Type::File;
    node.size = entry.size;
    text::copy(node.name, sizeof(node.name), entry.name);
    return node;
}

bool from_vfs(const vfs::Node& node, Node& entry) {
    return fs::entry(static_cast<std::uint32_t>(node.id), entry);
}

class ShrfsFileSystem final : public vfs::FileSystem {
public:
    const char* name() const override { return "shrfs"; }

    bool root(vfs::Node& node) override {
        Node entry;
        if (!fs::root(entry)) return false;
        node = to_vfs(entry);
        return true;
    }

    // SHRFS 的目錄不儲存子項目清單，每個項目記錄自己的父項目，因此查一個名字
    // 就是掃過目錄表挑出父項目相符又同名的那一個。
    //
    // A SHRFS directory stores no list of children; each entry records its own
    // parent, so looking up a name means scanning the table for the entry whose
    // parent matches and whose name is the one asked for.
    bool lookup(const vfs::Node& directory, const char* component,
                vfs::Node& result) override {
        Node parent;
        if (!from_vfs(directory, parent) || !parent.directory) return false;
        Node child;
        std::size_t position = 0;
        while (fs::list(parent, position, child)) {
            if (text::equals(child.name, component)) {
                result = to_vfs(child);
                return true;
            }
            ++position;
        }
        return false;
    }

    bool list(const vfs::Node& directory, std::size_t position, vfs::Node& child) override {
        Node parent;
        if (!from_vfs(directory, parent)) return false;
        Node entry;
        if (!fs::list(parent, position, entry)) return false;
        child = to_vfs(entry);
        return true;
    }

    io::Result read(const vfs::Node& file, std::uint64_t offset, void* buffer,
                    std::size_t length) override {
        Node entry;
        if (!from_vfs(file, entry)) return {0, io::Error::InvalidArgument};
        return fs::read(entry, offset, buffer, length);
    }

    // SHRFS1 是唯讀格式。可寫入的版本需要配置區塊與更新目錄表，那屬於之後的
    // 里程碑；在那之前明確回報不支援，比假裝寫成功誠實得多。
    //
    // SHRFS1 is a read-only format. A writable one needs block allocation and
    // an updated entry table, which belongs to a later milestone. Until then,
    // reporting it plainly is a great deal more honest than pretending a write
    // succeeded.
    io::Result write(const vfs::Node&, std::uint64_t, const void*, std::size_t) override {
        return {0, io::Error::Unsupported};
    }

    bool writable(const vfs::Node&) const override { return false; }
};

constinit ShrfsFileSystem shrfs_filesystem_object;

} // namespace

vfs::FileSystem& shrfs_filesystem() { return shrfs_filesystem_object; }

} // namespace shirley::fs
