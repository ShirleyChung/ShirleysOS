#include "shirley/block_cache.hpp"
#include "shirley/checksum.hpp"
#include "shirley/device.hpp"
#include "shirley/device_mount.hpp"
#include "shirley/exfat.hpp"
#include "shirley/ext4.hpp"
#include "shirley/heap.hpp"
#include "shirley/ram_disk.hpp"
#include "shirley/rootfs.hpp"
#include "shirley/vfs.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// 測試的映像是 tools/make-ext4-image.py 依 Linux 核心文件寫出來的
// tests/images/ext4-basic.img、ext4-metabg.img 與 ext2-basic.img，那支程式與核心
// 不共用任何程式碼。命令列上額外給的映像（例如 mke2fs 做的）只檢查能不能安全地
// 掛載並走完整棵樹：壞掉的映像可以掛載失敗，但不可以當掉或卡住。
//
// The images under test are tests/images/ext4-basic.img, ext4-metabg.img and
// ext2-basic.img, written from the Linux kernel documentation by
// tools/make-ext4-image.py, which shares no code with the kernel. Any further
// images on the command line (made by mke2fs, say) are only checked to mount
// and walk safely: a damaged image may fail to mount, but it must not crash or
// hang.

namespace {

using namespace shirley;

alignas(4096) unsigned char arena[2 * 1024 * 1024];

std::vector<std::uint8_t> load(const char* path) {
    std::ifstream file(path, std::ios::binary);
    assert(file && "cannot open the test image");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::string read_all(const char* path) {
    const int descriptor = vfs::open(path);
    assert(descriptor >= 0);
    std::string content;
    char chunk[700];
    for (;;) {
        const auto result = vfs::read(descriptor, chunk, sizeof(chunk));
        assert(result);
        if (result.transferred == 0) break;
        content.append(chunk, result.transferred);
    }
    vfs::close(descriptor);
    return content;
}

std::vector<std::string> names_in(const char* path) {
    vfs::Node directory;
    assert(vfs::stat(path, directory) && directory.directory());
    std::vector<std::string> names;
    vfs::Node child;
    for (std::size_t i = 0; vfs::list(directory, i, child); ++i) names.emplace_back(child.name);
    return names;
}

bool contains(const std::vector<std::string>& names, const char* name) {
    for (const auto& entry : names)
        if (entry == name) return true;
    return false;
}

std::string pattern(std::size_t length, unsigned step) {
    std::string bytes(length, '\0');
    for (std::size_t i = 0; i < length; ++i) bytes[i] = static_cast<char>((i * step + 1) & 0xFF);
    return bytes;
}

void test_crc32c() {
    // RFC 3720 附錄 B.4 的檢查值。 / The check value from RFC 3720, B.4.
    assert(checksum::crc32c("123456789", 9) == 0xE3069283u);
    assert(checksum::crc32c("6789", 4, checksum::crc32c("12345", 5)) == 0xE3069283u);
}

void test_ext4(std::vector<std::uint8_t> image) {
    io::RamDisk disk(image.data(), image.size());
    assert(ext4::identify(disk) && !exfat::identify(disk));
    io::BlockCache cache(disk);
    assert(cache.initialize(32));
    ext4::Volume volume;
    assert(volume.mount(cache) == ext4::MountError::None);
    assert(volume.mount(cache) == ext4::MountError::AlreadyMounted);
    assert(std::strcmp(volume.label(), "shirley-ext4") == 0);
    assert(std::strcmp(volume.name(), "ext4 (ro)") == 0);
    assert(volume.block_size() == 1024 && volume.group_count() == 32);
    assert(!volume.dirty());

    vfs::initialize();
    assert(vfs::mount("/", volume));

    // 裝置檔 null 與藏在 hello.txt 的 rec_len 裡的已刪除項目都不列出來。
    // Neither the device file null nor the deleted entry hidden in hello.txt's
    // rec_len is listed.
    const auto root = names_in("/");
    const std::string long_name(255, 'L');
    for (const char* name : {"lost+found", "hello.txt", "docs", "fragmented.bin", "sparse.bin",
                             "empty.txt", "名前-ünïcødé.txt", long_name.c_str(), "link-fast",
                             "link-slow", "shared.txt", "many", "indexed"})
        assert(contains(root, name));
    assert(root.size() == 13);
    assert(!contains(root, "null") && !contains(root, "ghost-entry"));

    assert(read_all("/hello.txt") == "Hello from ext4!\n");
    assert(read_all("/docs/readme.md") == pattern(3000, 7));
    assert(read_all("/docs/nested/deep/leaf.txt") == "deep leaf\n");
    assert(read_all("/docs/nested/../nested/deep/../../readme.md") == pattern(3000, 7));
    assert(read_all("/名前-ünïcødé.txt") == "unicode\n");
    assert(read_all(("/" + long_name).c_str()) == "long name\n");
    assert(read_all("/empty.txt").empty());
    // 符號連結讀出來是目標。 / A symbolic link reads as its target.
    assert(read_all("/link-fast") == "docs/readme.md");
    assert(read_all("/link-slow") == "docs/" + std::string(95, 'x'));

    // 深度 1 的 extent 樹，含洞與未初始化的 extent（磁碟上是 0xEE）。
    // A depth-1 extent tree with holes and an uninitialized extent (0xEE on
    // disk).
    const std::size_t block = 1024;
    auto fragmented = pattern(32 * block - 100, 13);
    for (const auto& [first, count] : {std::pair{5, 3}, {8, 2}, {10, 2}, {13, 7}, {24, 6}})
        for (std::size_t i = first * block; i < (first + count) * block && i < fragmented.size(); ++i)
            fragmented[i] = '\0';
    assert(read_all("/fragmented.bin") == fragmented);
    const int descriptor = vfs::open("/fragmented.bin");
    assert(descriptor >= 0 && vfs::seek(descriptor, 3 * block - 6));
    char middle[12];
    assert(vfs::read(descriptor, middle, sizeof(middle)).transferred == sizeof(middle));
    assert(std::string(middle, sizeof(middle)) == fragmented.substr(3 * block - 6, 12));
    vfs::close(descriptor);

    const auto sparse = read_all("/sparse.bin");
    assert(sparse.size() == 10u * 1024 * 1024);
    assert(sparse.substr(5000 * block, block) == pattern(block, 3));
    assert(sparse.find_first_not_of('\0') == 5000 * block);
    assert(sparse.find_last_not_of('\0') < 5001 * block);

    // 80 個硬連結，跨四個目錄區塊；htree 目錄裡另外五個。
    // Eighty hard links across four directory blocks; five more in an htree
    // directory.
    const auto many = names_in("/many");
    assert(many.size() == 80);
    for (int number = 0; number < 80; ++number) {
        char path[64];
        std::snprintf(path, sizeof(path), "/many/entry-%02d-with-a-longer-name.txt", number);
        assert(read_all(path) == "shared\n");
    }
    const auto indexed = names_in("/indexed");
    assert(indexed.size() == 5 && contains(indexed, "indexed-0") && contains(indexed, "indexed-4"));
    assert(read_all("/indexed/indexed-3") == "shared\n");
    assert(names_in("/lost+found").empty());

    vfs::Node node;
    assert(!vfs::stat("/missing.txt", node));
    assert(!vfs::stat("/HELLO.TXT", node));
    assert(!vfs::stat("/null", node));
    assert(vfs::stat("/many", node) && node.directory());
    assert(vfs::stat("/sparse.bin", node) && node.size == 10u * 1024 * 1024);
    assert(vfs::open("/docs") == vfs::error_is_directory);
    assert(vfs::open("/hello.txt", vfs::OpenFlags::Write) == vfs::error_read_only);
    assert(vfs::make_directory("/new") != io::Error::None);

    assert(vfs::unmount("/"));
    volume.unmount();
    assert(!volume.mounted());
    assert(cache.shutdown());
}

void test_ext2(std::vector<std::uint8_t> image) {
    io::RamDisk disk(image.data(), image.size());
    ext4::Volume volume;
    assert(volume.mount(disk) == ext4::MountError::None);
    assert(std::strcmp(volume.name(), "ext2 (ro)") == 0);
    assert(std::strcmp(volume.label(), "shirley-ext2") == 0);
    assert(volume.block_size() == 2048 && volume.group_count() == 2);
    vfs::initialize();
    assert(vfs::mount("/", volume));

    const auto root = names_in("/");
    assert(root.size() == 6);
    for (const char* name : {"lost+found", "hello.txt", "blocks.bin", "sparse.bin", "dir", "link"})
        assert(contains(root, name));
    assert(read_all("/hello.txt") == "Hello from ext2!\n");
    assert(read_all("/link") == "hello.txt");
    assert(read_all("/dir/sub/file.txt") == "nested\n");
    // 直接區塊加上一層間接區塊。 / Direct blocks plus a single indirect block.
    assert(read_all("/blocks.bin") == pattern(40 * 2048 - 5, 11));

    // 直接、一、二、三層間接區塊各一個區塊，其餘是洞。只讀那幾個區塊與它們的
    // 邊界，整個檔案有 500 MiB。
    //
    // One block behind each of the direct, single, double and triple indirect
    // pointers, the rest holes. Only those blocks and their edges are read; the
    // whole file is 500 MiB.
    const std::uint64_t block = 2048;
    const std::uint64_t per = block / 4;
    const std::uint64_t positions[] = {0, 12, 12 + per, 12 + per + per * per};
    const int descriptor = vfs::open("/sparse.bin");
    assert(descriptor >= 0);
    std::vector<char> buffer(block + 2);
    for (std::size_t index = 0; index < 4; ++index) {
        const auto at = positions[index] * block;
        assert(vfs::seek(descriptor, at == 0 ? 0 : at - 1));
        const auto want = at == 0 ? block + 1 : block + 2;
        const auto result = vfs::read(descriptor, buffer.data(), want);
        const bool last = index == 3;
        assert(result && result.transferred == (last ? want - 1 : want));
        std::size_t offset = 0;
        if (at != 0) assert(buffer[offset++] == 0);
        for (std::size_t i = 0; i < block; ++i)
            assert(buffer[offset + i] == static_cast<char>(index + 1));
        if (!last) assert(buffer[offset + block] == 0);
    }
    vfs::close(descriptor);

    assert(vfs::unmount("/"));
    volume.unmount();
}

// ext4 的超級區塊校驗和；改過超級區塊的測試要重算它才不會只測到校驗和。
// ext4's superblock checksum; tests that edit the superblock recompute it, so
// they test more than the checksum.
void reseal(std::vector<std::uint8_t>& image) {
    const auto sum = ~checksum::crc32c(image.data() + 1024, 1020);
    for (int i = 0; i < 4; ++i) image[1024 + 1020 + i] = static_cast<std::uint8_t>(sum >> (8 * i));
}

void test_rejections(const std::vector<std::uint8_t>& good) {
    ext4::Volume volume;

    io::RamDisk rootfs(fs::rootfs_image(), fs::rootfs_image_size());
    assert(!ext4::identify(rootfs));
    assert(volume.mount(rootfs) == ext4::MountError::NotExt4);

    auto image = good;
    image[1024 + 120] ^= 0x01;
    io::RamDisk changed(image.data(), image.size());
    assert(volume.mount(changed) == ext4::MountError::Corrupt);

    // needs_recovery。 / needs_recovery.
    image = good;
    image[1024 + 96] |= 0x04;
    reseal(image);
    io::RamDisk recover(image.data(), image.size());
    assert(volume.mount(recover) == ext4::MountError::NeedsRecovery);

    // inline_data 與不認得的 incompat 位元。 / inline_data and an unknown
    // incompat bit.
    for (const auto bit : {0x80u, 0x08u}) {
        image = good;
        image[1024 + 97] |= static_cast<std::uint8_t>(bit);
        reseal(image);
        io::RamDisk feature(image.data(), image.size());
        assert(volume.mount(feature) == ext4::MountError::Unsupported);
    }

    // 沒有乾淨卸載的卷冊仍然可以唯讀掛載。 / A volume not cleanly unmounted
    // still mounts read-only.
    image = good;
    image[1024 + 58] = 0;
    reseal(image);
    io::RamDisk unclean(image.data(), image.size());
    assert(volume.mount(unclean) == ext4::MountError::None && volume.dirty());
    volume.unmount();

    image = good;
    image[1024 + 40] ^= 0x01;
    reseal(image);
    io::RamDisk inodes(image.data(), image.size());
    assert(volume.mount(inodes) == ext4::MountError::Corrupt);

    image.assign(good.begin(), good.begin() + good.size() / 2);
    io::RamDisk truncated(image.data(), image.size());
    assert(volume.mount(truncated) == ext4::MountError::Corrupt);

    assert(!volume.mounted());
}

// 經過 fs::mount_device()：格式由魔數認出，exFAT 與 ext4 同時掛著。
// Through fs::mount_device(): the format is recognized by its magic number,
// with exFAT and ext4 mounted side by side.
void test_device_mount(std::vector<std::uint8_t> ext4_image, std::vector<std::uint8_t> exfat_image) {
    const auto heap_before = heap::available();
    device::initialize();
    io::RamDisk ext4_disk(ext4_image.data(), ext4_image.size());
    io::RamDisk exfat_disk(exfat_image.data(), exfat_image.size());
    io::RamDisk shrfs(fs::rootfs_image(), fs::rootfs_image_size());
    device::Device linux_disk{"sdb1", device::Type::Block, device::block_operations, &ext4_disk};
    device::Device usb{"sdc1", device::Type::Block, device::block_operations, &exfat_disk};
    device::Device other{"ram0", device::Type::Block, device::block_operations, &shrfs};
    for (auto* each : {&linux_disk, &usb, &other})
        assert(device::register_device(*each) == device::Status::Ok);
    vfs::initialize();
    assert(vfs::mount("/", vfs::devfs()));

    assert(std::strcmp(fs::mount_device("/ram0", "/other"),
                       "no supported file system (exFAT, ext2/3/4)") == 0);
    assert(fs::mount_device("sdb1", "linux", "/") == nullptr);
    assert(fs::mount_device("/sdc1", "/usb") == nullptr);
    assert(read_all("/linux/docs/nested/deep/leaf.txt") == "deep leaf\n");
    assert(read_all("/usb/README.TXT") == "Hello from exFAT!\n");
    bool listed = false;
    for (std::size_t i = 0; i < vfs::mount_count(); ++i)
        listed |= std::strcmp(vfs::mount_path(i), "/linux") == 0 &&
                  std::strcmp(vfs::mount_filesystem(i)->name(), "ext4 (ro)") == 0;
    assert(listed);
    assert(vfs::open("/linux/hello.txt", vfs::OpenFlags::Write) == vfs::error_read_only);

    assert(fs::unmount_device("/linux") == nullptr);
    assert(fs::unmount_device("/usb") == nullptr);
    vfs::Node gone;
    assert(!vfs::stat("/linux/hello.txt", gone));

    vfs::unmount("/");
    for (auto* each : {&other, &usb, &linux_disk}) device::unregister_device(*each);
    assert(heap::available() == heap_before);
}

std::size_t walk(const char* path, int depth) {
    vfs::Node directory;
    if (depth > 8 || !vfs::stat(path, directory)) return 0;
    std::size_t files = 0;
    vfs::Node child;
    for (std::size_t i = 0; i < 4096 && vfs::list(directory, i, child); ++i) {
        std::string next = std::string(path) + (path[1] == '\0' ? "" : "/") + child.name;
        if (child.directory()) {
            files += walk(next.c_str(), depth + 1);
            continue;
        }
        const int descriptor = vfs::open(next.c_str());
        if (descriptor < 0) continue;
        // 稀疏檔可能大得驚人；只讀開頭。 / A sparse file can be enormous; read
        // only the start.
        char chunk[4096];
        for (int i = 0; i < 64 && vfs::read(descriptor, chunk, sizeof(chunk)).transferred != 0; ++i) {}
        vfs::close(descriptor);
        ++files;
    }
    return files;
}

void test_external(const char* path) {
    auto image = load(path);
    io::RamDisk disk(image.data(), image.size());
    io::BlockCache cache(disk);
    assert(cache.initialize(32));
    ext4::Volume volume;
    const auto error = volume.mount(cache);
    std::printf("%s: %s", path, ext4::mount_error_text(error));
    if (error == ext4::MountError::None) {
        vfs::initialize();
        assert(vfs::mount("/", volume));
        std::printf(", %s, label \"%s\", %zu files read", volume.name(), volume.label(), walk("/", 0));
        vfs::unmount("/");
        volume.unmount();
    }
    std::printf("\n");
    assert(cache.shutdown());
}

} // namespace

int main(int argc, char** argv) {
    assert(argc >= 5 &&
           "usage: ext4_smoke <ext4-basic.img> <ext4-metabg.img> <ext2-basic.img> "
           "<exfat-clean.img> [more images...]");
    heap::initialize(arena, sizeof(arena));
    test_crc32c();
    const auto ext4_image = load(argv[1]);
    test_ext4(ext4_image);
    test_ext4(load(argv[2]));
    test_ext2(load(argv[3]));
    test_rejections(ext4_image);
    test_device_mount(ext4_image, load(argv[4]));
    for (int i = 5; i < argc; ++i) test_external(argv[i]);
    assert(heap::available() == heap::capacity() && heap::consistent());
    return 0;
}
