#include "shirley/block_cache.hpp"
#include "shirley/device.hpp"
#include "shirley/device_mount.hpp"
#include "shirley/exfat.hpp"
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

// 測試的映像是 tools/make-exfat-image.py 依規格寫出來的 tests/images/exfat-basic.img，
// 那支程式與核心不共用任何程式碼。命令列上額外給的映像（例如 mkfs.exfat 做的）
// 只檢查能不能安全地掛載並走完整棵樹：壞掉的映像可以掛載失敗，但不可以當掉或
// 卡住。
//
// The image under test is tests/images/exfat-basic.img, written from the
// specification by tools/make-exfat-image.py, which shares no code with the
// kernel. Any further images on the command line (made by mkfs.exfat, say) are
// only checked to mount and walk safely: a damaged image may fail to mount,
// but it must not crash or hang.

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

void test_fixture(std::vector<std::uint8_t> image) {
    io::RamDisk disk(image.data(), image.size());
    assert(exfat::identify(disk));
    io::BlockCache cache(disk);
    assert(cache.initialize(32));
    exfat::Volume volume;
    assert(volume.mount(cache) == exfat::MountError::None);
    assert(volume.mount(cache) == exfat::MountError::AlreadyMounted);
    assert(std::strcmp(volume.label(), "SHIRLEY") == 0);
    assert(volume.serial_number() == 0x5348524C);
    assert(volume.cluster_size() == 4096 && volume.cluster_count() == 224);
    assert(!volume.dirty());

    vfs::initialize();
    assert(vfs::mount("/", volume));

    // 根目錄：已刪除、校驗和錯誤、雜湊錯誤與名稱不合法的項目組都不出現。
    // The root: the deleted, bad-checksum, bad-hash and illegally named entry
    // sets do not appear.
    const auto root = names_in("/");
    assert(root.size() == 7);
    for (const char* name : {"README.TXT", "empty.txt", "fragmented.bin", "prealloc.bin",
                             "big.bin", "docs", "many"})
        assert(contains(root, name));
    for (const char* name : {"GHOST.TXT", "BROKEN.TXT", "BADHASH.TXT", "a:b.txt", ".."})
        assert(!contains(root, name));

    assert(read_all("/README.TXT") == "Hello from exFAT!\n");
    // 名稱不分大小寫。 / Names are case-insensitive.
    assert(read_all("/readme.txt") == "Hello from exFAT!\n");
    assert(read_all("/Docs/MIXED CASE.md") == "# mixed\n");
    assert(read_all("/empty.txt").empty());

    // 不照順序串接的叢集，以及跨越多個叢集的連續檔案。
    // Clusters chained out of order, and a contiguous file over several
    // clusters.
    assert(read_all("/fragmented.bin") == pattern(14000, 7));
    assert(read_all("/big.bin") == pattern(20000, 13));

    // ValidDataLength 之後讀成零，即使磁碟上寫著別的東西。
    // Past ValidDataLength reads as zero, whatever is on the disk there.
    const auto prealloc = read_all("/prealloc.bin");
    assert(prealloc.size() == 8192);
    assert(prealloc.substr(0, 5000) == pattern(5000, 3));
    assert(prealloc.substr(5000) == std::string(3192, '\0'));

    // 從中間開始讀，跨過叢集邊界。 / A read from the middle, across a cluster
    // boundary.
    const int descriptor = vfs::open("/fragmented.bin");
    assert(descriptor >= 0 && vfs::seek(descriptor, 4090));
    char middle[12];
    assert(vfs::read(descriptor, middle, sizeof(middle)).transferred == sizeof(middle));
    assert(std::string(middle, sizeof(middle)) == pattern(14000, 7).substr(4090, 12));
    vfs::close(descriptor);

    // 長的中文名稱：列得出來，也查得回去。
    // A long Chinese name: it lists, and it looks up again.
    const char* long_name = "這是一個很長的中文檔名_with_ASCII_and_more_than_fifteen_units.txt";
    const auto docs = names_in("/docs");
    assert(docs.size() == 3 && contains(docs, long_name) && contains(docs, "sub"));
    assert(read_all((std::string("/docs/") + long_name).c_str()) == "長檔名內容\n");
    assert(read_all("/docs/sub/deep.txt") == "deep inside\n");
    assert(read_all("/docs/sub/../../README.TXT") == "Hello from exFAT!\n");

    // many/ 跨兩個不相鄰的叢集，其中一組項目跨越邊界。
    // many/ spans two non-adjacent clusters, with one entry set straddling the
    // boundary.
    const auto many = names_in("/many");
    assert(many.size() == 60);
    for (int number = 0; number < 60; ++number) {
        char path[32];
        char expected[16];
        std::snprintf(path, sizeof(path), "/many/f%03d.txt", number);
        std::snprintf(expected, sizeof(expected), "file %03d\n", number);
        assert(read_all(path) == expected);
    }

    vfs::Node node;
    assert(!vfs::stat("/missing.txt", node));
    assert(vfs::stat("/many", node) && node.directory());
    assert(vfs::open("/docs") == vfs::error_is_directory);
    // 目前唯讀。 / Read-only for now.
    assert(vfs::open("/README.TXT", vfs::OpenFlags::Write) == vfs::error_read_only);

    assert(vfs::unmount("/"));
    volume.unmount();
    assert(!volume.mounted());
    assert(cache.shutdown());
}

void test_rejections(const std::vector<std::uint8_t>& good) {
    exfat::Volume volume;

    // 不是 exFAT。 / Not exFAT.
    io::RamDisk rootfs(fs::rootfs_image(), fs::rootfs_image_size());
    assert(!exfat::identify(rootfs));
    assert(volume.mount(rootfs) == exfat::MountError::NotExfat);

    // 開機磁區被改過但校驗和沒跟著改。 / The boot sector changed but the
    // checksum did not follow.
    auto image = good;
    image[100] ^= 0x01;
    io::RamDisk changed(image.data(), image.size());
    assert(volume.mount(changed) == exfat::MountError::Corrupt);

    // VolumeFlags 不在校驗和範圍內，改它不影響掛載。
    // VolumeFlags lies outside the checksum, so changing it does not stop the
    // mount.
    image = good;
    image[106] |= 0x02;
    io::RamDisk flagged(image.data(), image.size());
    assert(volume.mount(flagged) == exfat::MountError::None && volume.dirty());
    volume.unmount();

    // 大寫表內容被改壞。表在叢集 3，叢集堆積從磁區 256 開始。
    // The up-case table's content damaged. The table is in cluster 3, and the
    // cluster heap starts at sector 256.
    image = good;
    image[256 * 512 + 4096 + 200] ^= 0x01;
    io::RamDisk upcase(image.data(), image.size());
    assert(volume.mount(upcase) == exfat::MountError::Corrupt);

    // 映像被截短，宣稱的卷冊比裝置還大。 / A truncated image claims a volume
    // larger than the device.
    image.assign(good.begin(), good.begin() + good.size() / 2);
    io::RamDisk truncated(image.data(), image.size());
    assert(volume.mount(truncated) == exfat::MountError::Corrupt);

    assert(!volume.mounted());
}

// 從裝置註冊表掛載，走的是 shell 的 mount 指令那條路：devfs 找到裝置，
// mount_device() 在中間放一個區塊快取再掛上 exFAT。
//
// Mounting from the device registry, the path the shell's mount command takes:
// devfs finds the device, and mount_device() puts a block cache in between and
// mounts exFAT on it.
void test_device_mount(std::vector<std::uint8_t> image) {
    const auto heap_before = heap::available();
    device::initialize();
    io::RamDisk disk(image.data(), image.size());
    device::Device usb{"usb0", device::Type::Block, device::block_operations, &disk};
    io::RamDisk shrfs(fs::rootfs_image(), fs::rootfs_image_size());
    device::Device other{"ram0", device::Type::Block, device::block_operations, &shrfs};
    assert(device::register_device(usb) == device::Status::Ok);
    assert(device::register_device(other) == device::Status::Ok);
    vfs::initialize();
    assert(vfs::mount("/", vfs::devfs()));

    // 相對路徑相對於 base 解析。 / Relative paths resolve against base.
    assert(fs::mount_device("usb0", "mnt", "/") == nullptr);
    assert(read_all("/mnt/README.TXT") == "Hello from exFAT!\n");
    assert(read_all("/mnt/docs/sub/deep.txt") == "deep inside\n");
    assert(std::strcmp(fs::mount_device("/usb0", "/mnt"), "already mounted there") == 0);
    assert(std::strcmp(fs::mount_device("/missing", "/other"), "no such device") == 0);
    assert(std::strcmp(fs::mount_device("/ram0", "/other"), "not an exFAT volume") == 0);
    // 父目錄不存在的掛載點走不到，VFS 拒絕它。
    // A mount point whose parent does not exist is unreachable, and the VFS
    // refuses it.
    assert(std::strcmp(fs::mount_device("/usb0", "/no/such"), "the mount point is not usable") == 0);

    // 還有檔案開著就不能卸載。 / No unmounting while a file is open.
    const int descriptor = vfs::open("/mnt/big.bin");
    assert(descriptor >= 0);
    assert(std::strcmp(fs::unmount_device("/mnt"), "busy") == 0);
    vfs::close(descriptor);
    assert(fs::unmount_device("/mnt") == nullptr);
    assert(std::strcmp(fs::unmount_device("/mnt"), "not a mounted device") == 0);
    assert(std::strcmp(fs::unmount_device("/"), "not a mounted device") == 0);
    vfs::Node gone;
    assert(!vfs::stat("/mnt/README.TXT", gone));

    vfs::unmount("/");
    device::unregister_device(other);
    device::unregister_device(usb);
    // 失敗的掛載與卸載都把堆積還回去了。 / Failed mounts and the unmount all
    // gave the heap back.
    assert(heap::available() == heap_before);
}

// 走完整棵樹並讀出每個檔案。 / Walk the whole tree and read every file.
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
        char chunk[4096];
        while (vfs::read(descriptor, chunk, sizeof(chunk)).transferred != 0) {}
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
    exfat::Volume volume;
    const auto error = volume.mount(cache);
    std::printf("%s: %s", path, exfat::mount_error_text(error));
    if (error == exfat::MountError::None) {
        vfs::initialize();
        assert(vfs::mount("/", volume));
        std::printf(", label \"%s\", %zu files read", volume.label(), walk("/", 0));
        vfs::unmount("/");
        volume.unmount();
    }
    std::printf("\n");
    assert(cache.shutdown());
}

} // namespace

int main(int argc, char** argv) {
    assert(argc >= 2 && "usage: exfat_smoke <exfat-basic.img> [more images...]");
    heap::initialize(arena, sizeof(arena));
    const auto image = load(argv[1]);
    test_fixture(image);
    test_rejections(image);
    test_device_mount(image);
    for (int i = 2; i < argc; ++i) test_external(argv[i]);
    // 每次掛載用到的堆積都還回去了。 / Every mount gave its heap back.
    assert(heap::available() == heap::capacity() && heap::consistent());
    return 0;
}
