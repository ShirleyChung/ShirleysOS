#include "shirley/device.hpp"
#include "shirley/fs.hpp"
#include "shirley/input_queue.hpp"
#include "shirley/ram_disk.hpp"
#include "shirley/rootfs.hpp"
#include "shirley/vfs.hpp"

#include <cassert>
#include <cstring>

namespace {

using shirley::vfs::Node;
using shirley::vfs::OpenFlags;
using shirley::vfs::Type;

// 每個測試都從同一個起點開始：根檔案系統掛在 "/"，devfs 掛在 "/dev"，
// 和開機之後的狀態一樣。
//
// Every test starts from the same place: the root file system at "/" and devfs
// at "/dev", exactly as it is after boot.
void reset() {
    shirley::device::initialize();
    shirley::vfs::initialize();
    assert(shirley::device::null_initialize());
    assert(shirley::fs::mount_rootfs());
    assert(shirley::vfs::mount("/", shirley::fs::shrfs_filesystem()));
    assert(shirley::vfs::mount("/dev", shirley::vfs::devfs()));
}

// 路徑正規化：相對路徑、"."、".."、重複的斜線，全部在進到檔案系統之前處理完。
// Path normalization: relative paths, ".", "..", and repeated slashes are all
// resolved before any file system is asked anything.
void test_normalize() {
    char buffer[shirley::vfs::max_path_length];

    assert(shirley::vfs::normalize("/etc/version", nullptr, buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/etc/version") == 0);
    assert(shirley::vfs::normalize("//etc///version", nullptr, buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/etc/version") == 0);
    assert(shirley::vfs::normalize("/./etc/./version", nullptr, buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/etc/version") == 0);
    assert(shirley::vfs::normalize("/etc/../etc/version", nullptr, buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/etc/version") == 0);

    // 根目錄的上一層還是根目錄，往上走一定會停下來。
    // The root's parent is the root, so walking upwards always terminates.
    assert(shirley::vfs::normalize("/../../..", nullptr, buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/") == 0);
    assert(shirley::vfs::normalize("", nullptr, buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/") == 0);

    // 相對路徑相對於 base；以 '/' 開頭的忽略 base。
    // A relative path resolves against base; one starting with '/' ignores it.
    assert(shirley::vfs::normalize("version", "/etc", buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/etc/version") == 0);
    assert(shirley::vfs::normalize("..", "/etc", buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/") == 0);
    assert(shirley::vfs::normalize("/docs", "/etc", buffer, sizeof(buffer)));
    assert(std::strcmp(buffer, "/docs") == 0);

    // 放不下時回傳 false，而不是輸出一個指向別的檔案的截斷路徑。
    // Too small a buffer returns false rather than emitting a truncated path
    // that names a different file.
    char small[6];
    assert(!shirley::vfs::normalize("/etc/version", nullptr, small, sizeof(small)));
}

// 掛載表：第一個掛載必須是根，重複的掛載點會被拒絕，父目錄不存在也會被拒絕。
// The mount table: the first mount has to be the root, a duplicate mount point
// is refused, and so is one whose parent does not exist.
void test_mount_table() {
    shirley::device::initialize();
    shirley::vfs::initialize();
    assert(!shirley::vfs::mounted());
    // 還沒有根就掛別的地方，之後的路徑解析沒有起點。
    // Mounting elsewhere before there is a root would leave later resolution
    // with nowhere to start.
    assert(!shirley::vfs::mount("/dev", shirley::vfs::devfs()));

    assert(shirley::fs::mount_rootfs());
    assert(shirley::vfs::mount("/", shirley::fs::shrfs_filesystem()));
    assert(shirley::vfs::mounted());
    assert(shirley::vfs::mount_count() == 1);
    assert(!shirley::vfs::mount("/", shirley::vfs::devfs()));

    assert(shirley::vfs::mount("/dev", shirley::vfs::devfs()));
    assert(shirley::vfs::mount_count() == 2);
    assert(std::strcmp(shirley::vfs::mount_path(1), "/dev") == 0);
    assert(std::strcmp(shirley::vfs::mount_filesystem(1)->name(), "devfs") == 0);
    // 父目錄不存在的掛載點，除了打完整路徑之外沒有辦法走到。
    // A mount point whose parent does not exist could never be reached except
    // by naming its full path.
    assert(!shirley::vfs::mount("/nowhere/deep", shirley::vfs::devfs()));

    assert(shirley::vfs::unmount("/dev"));
    assert(shirley::vfs::mount_count() == 1);
    assert(!shirley::vfs::unmount("/dev"));
}

// 最長的掛載點才是該回答的那一個：`/dev/null` 同時在 `/` 與 `/dev` 之下。
// The longest mount point is the one that answers: `/dev/null` lies under both
// `/` and `/dev`.
void test_resolution_picks_longest_mount() {
    reset();
    Node node;
    assert(shirley::vfs::stat("/etc/version", node));
    assert(std::strcmp(node.filesystem->name(), "shrfs") == 0);
    assert(node.type == Type::File);
    assert(node.size > 0);
    assert(std::strcmp(node.path, "/etc/version") == 0);

    assert(shirley::vfs::stat("/dev", node));
    assert(std::strcmp(node.filesystem->name(), "devfs") == 0);
    assert(node.directory());

    assert(shirley::vfs::stat("/dev/null", node));
    assert(std::strcmp(node.filesystem->name(), "devfs") == 0);
    assert(node.type == Type::CharacterDevice);
    assert(node.device == shirley::device::find("null"));

    // 字串前綴不等於路徑前綴。
    // A string prefix is not a path prefix.
    assert(!shirley::vfs::stat("/devices", node));
    assert(!shirley::vfs::stat("/etc/missing", node));
    // 檔案不是目錄，不能往下走。
    // A file is not a directory and cannot be walked into.
    assert(!shirley::vfs::stat("/etc/version/more", node));
}

// 列出目錄時，掛在它底下的檔案系統要以一個項目的形式出現，即使底層的檔案
// 系統裡沒有那個目錄。
//
// A listing shows a file system mounted inside the directory as an entry, even
// though the underlying file system has no such directory.
void test_listing_includes_mount_points() {
    reset();
    Node root;
    assert(shirley::vfs::stat("/", root));

    bool saw_etc = false;
    bool saw_dev = false;
    Node child;
    std::size_t position = 0;
    while (shirley::vfs::list(root, position, child)) {
        if (std::strcmp(child.name, "etc") == 0) saw_etc = true;
        if (std::strcmp(child.name, "dev") == 0) {
            saw_dev = true;
            assert(child.directory());
            assert(std::strcmp(child.path, "/dev") == 0);
            assert(std::strcmp(child.filesystem->name(), "devfs") == 0);
        }
        ++position;
    }
    assert(saw_etc);
    assert(saw_dev);
    assert(position == shirley::vfs::child_count(root));

    // 掛載點自己的內容是它那個檔案系統的根，不是父目錄裡的一個項目。
    // A mount point's content is its file system's root rather than an entry
    // in the parent directory.
    Node dev;
    assert(shirley::vfs::stat("/dev", dev));
    assert(shirley::vfs::child_count(dev) == shirley::device::count());
}

// devfs 的內容就是裝置註冊表，兩者不可能不一致。
// devfs's content is the device registry, and the two can never disagree.
void test_devfs_tracks_the_registry() {
    reset();
    Node node;
    assert(!shirley::vfs::stat("/dev/kbd0", node));

    shirley::io::InputQueue queue;
    shirley::device::Device keyboard{"kbd0", shirley::device::Type::Input,
                                     shirley::device::stream_operations, &queue};
    assert(shirley::device::register_device(keyboard) == shirley::device::Status::Ok);
    assert(shirley::vfs::stat("/dev/kbd0", node));
    assert(node.device == &keyboard);

    // 讀 /dev/kbd0 讀到的就是那個佇列裡的字元。
    // Reading /dev/kbd0 yields the characters in that very queue.
    assert(queue.push('h'));
    assert(queue.push('i'));
    const auto descriptor = shirley::vfs::open("/dev/kbd0");
    assert(descriptor >= 0);
    char buffer[8]{};
    const auto result = shirley::vfs::read(descriptor, buffer, sizeof(buffer));
    assert(result);
    assert(result.transferred == 2);
    assert(std::strncmp(buffer, "hi", 2) == 0);
    assert(shirley::vfs::close(descriptor));

    assert(shirley::device::unregister_device(keyboard) == shirley::device::Status::Ok);
    assert(!shirley::vfs::stat("/dev/kbd0", node));
}

// open/read/close：讀到結尾回傳 0 個位元組，位置會跟著前進，seek 回頭再讀
// 要讀到同一份內容。
//
// open/read/close: the end of a file yields zero bytes, the position advances,
// and seeking back and reading again returns the same content.
void test_open_read_close() {
    reset();
    const auto descriptor = shirley::vfs::open("/etc/version");
    assert(descriptor >= 0);
    const auto* node = shirley::vfs::node_of(descriptor);
    assert(node != nullptr && node->size > 0);

    char first[64]{};
    auto result = shirley::vfs::read(descriptor, first, 4);
    assert(result && result.transferred == 4);
    assert(shirley::vfs::position(descriptor) == 4);
    assert(std::strncmp(first, "Shir", 4) == 0);

    char rest[64]{};
    result = shirley::vfs::read(descriptor, rest, sizeof(rest));
    assert(result && result.transferred == node->size - 4);
    // 讀到結尾之後是 0 個位元組，不是錯誤。
    // Past the end is zero bytes rather than an error.
    result = shirley::vfs::read(descriptor, rest, sizeof(rest));
    assert(result && result.transferred == 0);

    assert(shirley::vfs::seek(descriptor, 0));
    char again[64]{};
    result = shirley::vfs::read(descriptor, again, 4);
    assert(result && result.transferred == 4);
    assert(std::strncmp(again, first, 4) == 0);

    assert(shirley::vfs::close(descriptor));
    // 關掉之後描述子就不再有效。
    // A closed descriptor is no longer valid.
    assert(shirley::vfs::node_of(descriptor) == nullptr);
    assert(!shirley::vfs::close(descriptor));
    assert(!shirley::vfs::read(descriptor, again, 1));
}

// 開啟的失敗原因要分得出來，shell 才說得出到底哪裡不對。
// The reasons an open fails have to be distinguishable, or a shell cannot say
// what actually went wrong.
void test_open_failures() {
    reset();
    assert(shirley::vfs::open("/etc/missing") == shirley::vfs::error_not_found);
    assert(shirley::vfs::open("/etc") == shirley::vfs::error_is_directory);
    // SHRFS1 是唯讀格式，因此要求寫入在開啟時就被拒絕。
    // SHRFS1 is read-only, so asking to write is refused at open.
    assert(shirley::vfs::open("/etc/version", OpenFlags::Write) == shirley::vfs::error_read_only);
    assert(shirley::vfs::open(nullptr) == shirley::vfs::error_invalid);

    // 描述子用完時明確失敗，而不是覆寫別人的。
    // Running out of descriptors fails plainly rather than overwriting one.
    int descriptors[shirley::vfs::max_open_files];
    for (std::size_t index = 0; index < shirley::vfs::max_open_files; ++index) {
        descriptors[index] = shirley::vfs::open("/etc/version");
        assert(descriptors[index] >= 0);
    }
    assert(shirley::vfs::open("/etc/version") == shirley::vfs::error_no_descriptors);
    // 關掉一個之後又能開一個。
    // Closing one makes room again.
    assert(shirley::vfs::close(descriptors[0]));
    const auto reopened = shirley::vfs::open("/etc/version");
    assert(reopened >= 0);
    assert(shirley::vfs::close(reopened));
    for (std::size_t index = 1; index < shirley::vfs::max_open_files; ++index) {
        assert(shirley::vfs::close(descriptors[index]));
    }
}

// null 裝置可以寫入，鍵盤不行，兩者都在 devfs 底下——寫入的能力是每個裝置
// 自己的事，不是整個檔案系統的。
//
// The null device can be written and a keyboard cannot, and both live under
// devfs: writability belongs to a device rather than to the file system.
void test_device_writes() {
    reset();
    const auto null = shirley::vfs::open("/dev/null", OpenFlags::Read | OpenFlags::Write);
    assert(null >= 0);
    const auto written = shirley::vfs::write(null, "discarded", 9);
    assert(written && written.transferred == 9);
    char buffer[4]{};
    const auto read = shirley::vfs::read(null, buffer, sizeof(buffer));
    assert(read && read.transferred == 0);
    assert(shirley::vfs::close(null));

    shirley::io::InputQueue queue;
    shirley::device::Device keyboard{"kbd0", shirley::device::Type::Input,
                                     shirley::device::stream_operations, &queue};
    assert(shirley::device::register_device(keyboard) == shirley::device::Status::Ok);
    assert(shirley::vfs::open("/dev/kbd0", OpenFlags::Write) == shirley::vfs::error_read_only);
}

// 區塊裝置：open() 之後可以用 block_read/block_write 直接指定磁區，也可以用
// 位元組層的 read/write 讓 VFS 自己換算。
//
// A block device: after open() it can be addressed by sector through
// block_read/block_write, or by byte through read/write with the VFS doing the
// translation.
void test_block_device() {
    reset();
    // 用一塊自己的記憶體，不要去動已經掛起來的根檔案系統。
    // Use memory of its own rather than disturbing the mounted root file
    // system.
    static unsigned char storage[2048];
    for (std::size_t index = 0; index < sizeof(storage); ++index)
        storage[index] = static_cast<unsigned char>(index);
    shirley::io::RamDisk disk(storage, sizeof(storage), 512);
    shirley::device::Device scratch{"scratch0", shirley::device::Type::Block,
                                    shirley::device::block_operations, &disk};
    assert(shirley::device::register_device(scratch) == shirley::device::Status::Ok);
    assert(scratch.is_block_device());

    Node node;
    assert(shirley::vfs::stat("/dev/scratch0", node));
    assert(node.type == Type::BlockDevice);
    // 區塊裝置的大小就是它的容量。
    // A block device's size is its capacity.
    assert(node.size == sizeof(storage));

    const auto descriptor = shirley::vfs::open("/dev/scratch0", OpenFlags::Read | OpenFlags::Write);
    assert(descriptor >= 0);
    assert(shirley::vfs::block_size(descriptor) == 512);
    assert(shirley::vfs::block_count(descriptor) == 4);

    unsigned char block[512]{};
    auto result = shirley::vfs::block_read(descriptor, 1, 1, block);
    assert(result);
    assert(block[0] == static_cast<unsigned char>(512));
    assert(block[7] == static_cast<unsigned char>(519));

    // 寫一整塊，再讀回來確認寫進去的就是那些位元組。
    // Write a whole block, then read it back to confirm those are the bytes
    // that landed.
    for (std::size_t index = 0; index < sizeof(block); ++index) block[index] = 0xa5;
    result = shirley::vfs::block_write(descriptor, 3, 1, block);
    assert(result);
    unsigned char verify[512]{};
    result = shirley::vfs::block_read(descriptor, 3, 1, verify);
    assert(result);
    for (std::size_t index = 0; index < sizeof(verify); ++index) assert(verify[index] == 0xa5);
    // 相鄰的區塊不可以被動到。
    // The neighbouring block must not have been touched.
    assert(storage[512 * 3 - 1] == static_cast<unsigned char>(512 * 3 - 1));

    // 位元組層的存取不必對齊到區塊邊界：跨越邊界的一段要讀出連續的內容。
    // Byte access need not be block-aligned: a range crossing a boundary has
    // to come back contiguous.
    assert(shirley::vfs::seek(descriptor, 510));
    unsigned char bytes[4]{};
    result = shirley::vfs::read(descriptor, bytes, sizeof(bytes));
    assert(result && result.transferred == 4);
    assert(bytes[0] == static_cast<unsigned char>(510));
    assert(bytes[1] == static_cast<unsigned char>(511));
    assert(bytes[2] == static_cast<unsigned char>(512));
    assert(bytes[3] == static_cast<unsigned char>(513));

    // 只改到一塊的一部分時，同一塊裡其他的位元組必須原封不動。
    // Overwriting part of a block must leave the rest of that block alone.
    assert(shirley::vfs::seek(descriptor, 4));
    const unsigned char patch[2] = {0xde, 0xad};
    result = shirley::vfs::write(descriptor, patch, sizeof(patch));
    assert(result && result.transferred == 2);
    assert(storage[4] == 0xde && storage[5] == 0xad);
    assert(storage[3] == 3 && storage[6] == 6);

    // 讀到裝置結尾之後是 0 個位元組，不是錯誤。
    // Past the end of the device is zero bytes rather than an error.
    assert(shirley::vfs::seek(descriptor, sizeof(storage)));
    result = shirley::vfs::read(descriptor, bytes, sizeof(bytes));
    assert(result && result.transferred == 0);
    assert(shirley::vfs::close(descriptor));
}

// 一般檔案不是區塊裝置，區塊層的存取必須明確被拒絕，而不是回答一塊垃圾。
// An ordinary file is not a block device, and block-level access has to be
// refused plainly rather than answered with a block of rubbish.
void test_block_access_refused_on_files() {
    reset();
    const auto descriptor = shirley::vfs::open("/etc/version");
    assert(descriptor >= 0);
    unsigned char block[512]{};
    const auto result = shirley::vfs::block_read(descriptor, 0, 1, block);
    assert(!result);
    assert(result.error == shirley::io::Error::Unsupported);
    assert(shirley::vfs::block_size(descriptor) == 0);
    assert(shirley::vfs::block_count(descriptor) == 0);
    assert(shirley::vfs::close(descriptor));
}

// 根檔案系統所在的磁碟本身也是一個裝置，因此可以直接看它的區塊。映像的第一
// 個位元組就是 SHRFS1 的識別字，那是檔案系統掛載時檢查的同一份位元組。
//
// The disk the root file system lives on is a device too, so its blocks can be
// looked at directly. The image's first bytes are the SHRFS1 magic, the very
// bytes the file system checks when it mounts.
void test_root_disk_is_a_device() {
    reset();
    assert(shirley::device::find("ram0") == &shirley::fs::rootfs_device());
    const auto descriptor = shirley::vfs::open("/dev/ram0");
    assert(descriptor >= 0);
    assert(shirley::vfs::block_size(descriptor) == 512);

    unsigned char block[512]{};
    const auto result = shirley::vfs::block_read(descriptor, 0, 1, block);
    assert(result);
    assert(std::strncmp(reinterpret_cast<const char*>(block), "SHRFS1", 6) == 0);
    assert(shirley::vfs::close(descriptor));
}

// read_file() 是 ELF loader 讀程式走的那條路：整個檔案要進到緩衝區，放不下
// 就回報 0，絕不交出一份被截斷的映像。
//
// read_file() is the path the ELF loader takes to read a program: the whole
// file has to reach the buffer, and one that does not fit reports zero rather
// than handing back a truncated image.
void test_read_file() {
    reset();
    char buffer[256]{};
    const auto size = shirley::vfs::read_file("/etc/version", buffer, sizeof(buffer));
    assert(size > 0);
    assert(std::strncmp(buffer, "ShirleyOS", 9) == 0);

    Node node;
    assert(shirley::vfs::stat("/etc/version", node));
    assert(size == node.size);

    char small[4]{};
    assert(shirley::vfs::read_file("/etc/version", small, sizeof(small)) == 0);
    assert(shirley::vfs::read_file("/etc/missing", buffer, sizeof(buffer)) == 0);
}

} // namespace

int main() {
    test_normalize();
    test_mount_table();
    test_resolution_picks_longest_mount();
    test_listing_includes_mount_points();
    test_devfs_tracks_the_registry();
    test_open_read_close();
    test_open_failures();
    test_device_writes();
    test_block_device();
    test_block_access_refused_on_files();
    test_root_disk_is_a_device();
    test_read_file();
    return 0;
}
