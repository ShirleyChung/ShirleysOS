#include "shirley/fs.hpp"
#include "shirley/ram_disk.hpp"
#include "shirley/rootfs.hpp"
#include "shirley/text.hpp"

#include <cassert>
#include <cstring>

namespace {

using shirley::fs::Node;

// 測試的是真正會開機的那份映像：rootfs/ 由建置流程打包，核心與這個測試連結
// 的是同一份位元組，因此這裡通過就代表開機後看到的檔案系統也是這樣。
//
// These tests run against the very image that boots: the build packs rootfs/
// and both the kernel and this test link the same bytes, so what passes here
// is what the file system looks like after boot.
shirley::io::RamDisk& rootfs_disk() {
    static shirley::io::RamDisk disk(shirley::fs::rootfs_image(), shirley::fs::rootfs_image_size());
    return disk;
}

void test_text_helpers() {
    assert(shirley::text::length("shirley") == 7);
    assert(shirley::text::equals("ls", "ls"));
    assert(!shirley::text::equals("ls", "lst"));
    assert(shirley::text::equals("later", "lame", 2));
    assert(!shirley::text::equals("later", "lame", 3));

    char buffer[8];
    assert(shirley::text::copy(buffer, sizeof(buffer), "shirley"));
    assert(std::strcmp(buffer, "shirley") == 0);
    // 放不下時目的地必須是空字串，截斷後的路徑會指向另一個檔案。
    // What does not fit leaves an empty destination: a truncated path would
    // name a different file.
    assert(!shirley::text::copy(buffer, sizeof(buffer), "shirleyos"));
    assert(buffer[0] == '\0');

    assert(shirley::text::copy(buffer, sizeof(buffer), "/etc"));
    assert(shirley::text::append(buffer, sizeof(buffer), "/x"));
    assert(std::strcmp(buffer, "/etc/x") == 0);
    // 放不下的附加不得動到既有內容。
    // An append that does not fit must leave the existing content alone.
    assert(!shirley::text::append(buffer, sizeof(buffer), "/yy"));
    assert(std::strcmp(buffer, "/etc/x") == 0);
}

void test_mount() {
    assert(!shirley::fs::mounted());
    assert(shirley::fs::mount(rootfs_disk()));
    assert(shirley::fs::mounted());
    assert(shirley::fs::entry_count() > 1);
    assert(shirley::fs::total_file_bytes() > 0);

    Node root;
    assert(shirley::fs::root(root));
    assert(root.directory);
    assert(root.index == 0);
    // 根目錄的父項目是自己，往上走才會停下來而不需要特例。
    // The root is its own parent, which is what makes walking upwards
    // terminate without a special case.
    assert(root.parent == 0);
    assert(shirley::fs::child_count(root) > 0);
}

// 損壞的映像不可以掛載成功：晚一步發現只會變成第一個 ls 的怪異行為。
// A corrupt image must not mount. Finding out later only turns into strange
// behaviour on the first ls.
void test_rejects_bad_image() {
    unsigned char rubbish[1024]{};
    shirley::io::RamDisk disk(rubbish, sizeof(rubbish));
    assert(!shirley::fs::mount(disk));
    assert(!shirley::fs::mounted());
    // 掛載失敗之後必須維持未掛載，不能留著上一次的狀態。
    // A failed mount stays unmounted rather than leaving the previous state
    // in place.
    Node node;
    assert(!shirley::fs::lookup("/etc", node));
    assert(shirley::fs::mount(rootfs_disk()));
}

void test_lookup() {
    Node node;
    assert(shirley::fs::lookup("/etc", node));
    assert(node.directory);
    assert(std::strcmp(node.name, "etc") == 0);

    assert(shirley::fs::lookup("/etc/version", node));
    assert(!node.directory);
    assert(node.size > 0);

    // 重複的斜線與 "." 不改變結果。
    // Repeated slashes and "." change nothing.
    Node same;
    assert(shirley::fs::lookup("//etc///version", same));
    assert(same.index == node.index);
    assert(shirley::fs::lookup("/./etc/./version", same));
    assert(same.index == node.index);
    // ".." 從根目錄往上走仍然是根目錄。
    // ".." at the root still lands on the root.
    assert(shirley::fs::lookup("/etc/../etc/version", same));
    assert(same.index == node.index);
    assert(shirley::fs::lookup("/../..", same));
    assert(same.index == 0);

    assert(!shirley::fs::lookup("/etc/missing", node));
    // 檔案不是目錄，不能往下走。
    // A file is not a directory and cannot be walked into.
    assert(!shirley::fs::lookup("/etc/version/more", node));
}

void test_relative_lookup() {
    Node base;
    assert(shirley::fs::lookup("/etc", base));
    Node node;
    assert(shirley::fs::lookup("version", node, &base));
    assert(!node.directory);
    // 相對路徑的 ".." 回到父目錄。
    // A relative ".." goes back to the parent.
    Node parent;
    assert(shirley::fs::lookup("..", parent, &base));
    assert(parent.index == 0);
    // 以 '/' 開頭的路徑忽略 base。
    // A path starting with '/' ignores base.
    Node absolute;
    assert(shirley::fs::lookup("/etc", absolute, &base));
    assert(absolute.index == base.index);
}

void test_listing() {
    Node root;
    assert(shirley::fs::root(root));
    const auto count = shirley::fs::child_count(root);
    assert(count >= 3);

    bool saw_etc = false;
    Node child;
    std::size_t position = 0;
    while (shirley::fs::list(root, position, child)) {
        // 根目錄本身絕對不能出現在自己的清單裡。
        // The root itself must never appear in its own listing.
        assert(child.index != 0);
        assert(child.parent == root.index);
        if (std::strcmp(child.name, "etc") == 0) saw_etc = true;
        ++position;
    }
    assert(position == count);
    assert(saw_etc);

    // 檔案沒有子項目，列出檔案是錯誤而不是空清單。
    // A file has no children, and listing one is an error rather than an
    // empty listing.
    Node file;
    assert(shirley::fs::lookup("/etc/version", file));
    assert(!shirley::fs::list(file, 0, child));
    assert(shirley::fs::child_count(file) == 0);
}

void test_read() {
    Node node;
    assert(shirley::fs::lookup("/etc/version", node));
    char buffer[64]{};
    const auto result = shirley::fs::read(node, 0, buffer, sizeof(buffer) - 1);
    assert(result);
    assert(result.transferred == node.size);
    assert(std::strncmp(buffer, "ShirleyOS", 9) == 0);

    // 從中間讀，讀到的必須是同一份內容的後半段。
    // A read from the middle returns the same content's later bytes.
    char tail[64]{};
    const auto partial = shirley::fs::read(node, 4, tail, sizeof(tail) - 1);
    assert(partial);
    assert(partial.transferred == node.size - 4);
    assert(std::strncmp(tail, buffer + 4, static_cast<std::size_t>(partial.transferred)) == 0);

    // 讀到結尾之後回傳零個位元組，而不是錯誤。
    // Reading past the end yields zero bytes rather than an error.
    const auto beyond = shirley::fs::read(node, node.size, buffer, sizeof(buffer));
    assert(beyond);
    assert(beyond.transferred == 0);

    // 目錄不能當成檔案讀。
    // A directory cannot be read as a file.
    Node directory;
    assert(shirley::fs::lookup("/etc", directory));
    const auto refused = shirley::fs::read(directory, 0, buffer, sizeof(buffer));
    assert(!refused);
}

void test_path_of() {
    char path[shirley::fs::max_path_length];
    Node root;
    assert(shirley::fs::root(root));
    assert(shirley::fs::path_of(root, path, sizeof(path)));
    assert(std::strcmp(path, "/") == 0);

    Node node;
    assert(shirley::fs::lookup("/etc/version", node));
    assert(shirley::fs::path_of(node, path, sizeof(path)));
    assert(std::strcmp(path, "/etc/version") == 0);

    // 組回來的路徑必須指向同一個項目，"../.." 這種寫法也一樣。
    // The assembled path has to resolve to the same entry, including when the
    // original was written with "../..".
    Node again;
    assert(shirley::fs::lookup(path, again));
    assert(again.index == node.index);

    // 緩衝區不足時回傳 false，而不是輸出截斷的路徑。
    // Too small a buffer returns false rather than a truncated path.
    char small[4];
    assert(!shirley::fs::path_of(node, small, sizeof(small)));
}

} // namespace

int main() {
    test_text_helpers();
    test_mount();
    test_rejects_bad_image();
    test_lookup();
    test_relative_lookup();
    test_listing();
    test_read();
    test_path_of();
    return 0;
}
