#include "shirley/block_cache.hpp"
#include "shirley/exfat.hpp"
#include "shirley/heap.hpp"
#include "shirley/ram_disk.hpp"
#include "shirley/vfs.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// exFAT 寫入。從 tests/images/exfat-clean.img 的一份複本出發，經過 VFS 做每一種
// 寫入操作，每一步都讀回來比對，最後乾淨地卸載並把映像存到命令列給的路徑。
// 那份映像接著交給 tools/check-exfat-image.py——一個不與核心共用程式碼、依規格
// 寫成的檢查程式——確認位元圖、FAT 鏈、校驗和與名稱雜湊都一致。
//
// exFAT writing. Starting from a copy of tests/images/exfat-clean.img, every
// kind of write goes through the VFS and each step is read back, then the
// volume is cleanly unmounted and the image saved to the path on the command
// line. That image then goes to tools/check-exfat-image.py — a checker written
// from the specification, sharing no code with the kernel — which confirms the
// bitmap, FAT chains, checksums and name hashes all agree.

namespace {

using namespace shirley;

alignas(4096) unsigned char arena[2 * 1024 * 1024];

constexpr auto read_write = vfs::OpenFlags::Read | vfs::OpenFlags::Write;
constexpr auto create = vfs::OpenFlags::Read | vfs::OpenFlags::Write | vfs::OpenFlags::Create;

std::string read_all(const char* path) {
    const int descriptor = vfs::open(path);
    assert(descriptor >= 0);
    std::string content;
    char chunk[1000];
    for (;;) {
        const auto result = vfs::read(descriptor, chunk, sizeof(chunk));
        assert(result);
        if (result.transferred == 0) break;
        content.append(chunk, result.transferred);
    }
    vfs::close(descriptor);
    return content;
}

void write_file(const char* path, const std::string& content, vfs::OpenFlags flags = create) {
    const int descriptor = vfs::open(path, flags);
    assert(descriptor >= 0);
    const auto written = vfs::write(descriptor, content.data(), content.size());
    assert(written && written.transferred == content.size());
    vfs::close(descriptor);
}

std::string pattern(std::size_t length, unsigned seed) {
    std::string bytes(length, '\0');
    for (std::size_t i = 0; i < length; ++i) bytes[i] = static_cast<char>((i * 31 + seed) & 0xFF);
    return bytes;
}

bool exists(const char* path) {
    vfs::Node node;
    return vfs::stat(path, node);
}

std::uint64_t size_of(const char* path) {
    vfs::Node node;
    assert(vfs::stat(path, node));
    return node.size;
}

void test_create_and_write() {
    write_file("/new.txt", "hello");
    assert(read_all("/new.txt") == "hello");
    assert(size_of("/new.txt") == 5);
    // 已存在的檔案以 Create 開啟不會被清空。 / Opening an existing file with
    // Create does not empty it.
    const int again = vfs::open("/NEW.TXT", create);
    assert(again >= 0);
    vfs::close(again);
    assert(read_all("/new.txt") == "hello");

    // 跨越好幾個叢集、分好幾次寫。 / Several writes across several clusters.
    const auto big = pattern(10000, 7);
    const int descriptor = vfs::open("/grow.bin", create);
    assert(descriptor >= 0);
    for (std::size_t at = 0; at < big.size(); at += 3000) {
        const auto take = big.size() - at < 3000 ? big.size() - at : 3000;
        assert(vfs::write(descriptor, big.data() + at, take).transferred == take);
    }
    vfs::close(descriptor);
    assert(read_all("/grow.bin") == big);

    // 寫在檔案結尾之後：中間的空隙讀起來是零。 / A write past the end: the
    // gap in between reads as zeros.
    const int sparse = vfs::open("/grow.bin", read_write);
    assert(vfs::seek(sparse, 20000));
    assert(vfs::write(sparse, "X", 1).transferred == 1);
    vfs::close(sparse);
    const auto content = read_all("/grow.bin");
    assert(content.size() == 20001);
    assert(content.substr(0, 10000) == big);
    assert(content.substr(10000, 10000) == std::string(10000, '\0'));
    assert(content[20000] == 'X');

    // 覆寫中間一段，不改長度。 / Overwrite a stretch in the middle, keeping
    // the length.
    const int middle = vfs::open("/grow.bin", read_write);
    assert(vfs::seek(middle, 4090));
    assert(vfs::write(middle, "ACROSS-A-BOUNDARY", 17).transferred == 17);
    vfs::close(middle);
    const auto patched = read_all("/grow.bin");
    assert(patched.size() == 20001 && patched.substr(4090, 17) == "ACROSS-A-BOUNDARY");
    assert(patched.substr(0, 4090) == big.substr(0, 4090));
}

void test_directories() {
    assert(vfs::make_directory("/made") == io::Error::None);
    assert(vfs::make_directory("/made") == io::Error::AlreadyExists);
    assert(vfs::make_directory("/missing/child") == io::Error::NotFound);
    write_file("/made/inner.txt", "inside");
    assert(read_all("/made/inner.txt") == "inside");

    // 60 個檔案、180 個項目，超過一個叢集的 128 個：目錄必須長大。
    // 60 files and 180 entries, past one cluster's 128: the directory has to
    // grow.
    for (int number = 0; number < 60; ++number) {
        char path[48];
        std::snprintf(path, sizeof(path), "/made/file-%02d.txt", number);
        write_file(path, std::string(path));
    }
    for (int number = 0; number < 60; ++number) {
        char path[48];
        std::snprintf(path, sizeof(path), "/made/file-%02d.txt", number);
        assert(read_all(path) == path);
    }
    assert(size_of("/made") == 2 * 4096);

    // 長的中文名稱。 / A long Chinese name.
    const char* chinese = "/made/這是一個寫進去的很長的中文檔名_and_some_ASCII_too.txt";
    write_file(chinese, "長檔名寫入\n");
    assert(read_all(chinese) == "長檔名寫入\n");

    // 不合法的名稱被拒絕。 / Illegal names are refused.
    assert(vfs::open("/made/bad:name", create) == vfs::error_invalid);
    assert(vfs::make_directory("/made/what?") == io::Error::InvalidArgument);
}

void test_fragmentation() {
    // 兩個檔案各一個叢集、緊緊相鄰；再讓第一個長大，它就接不上了，必須改成
    // FAT 串接。
    // Two one-cluster files right next to each other; growing the first means
    // it can no longer continue in place and has to become FAT-chained.
    const auto first = pattern(4000, 1);
    const auto second = pattern(4000, 2);
    write_file("/first.bin", first);
    write_file("/second.bin", second);
    const auto more = pattern(9000, 3);
    const int descriptor = vfs::open("/first.bin", read_write);
    assert(vfs::seek(descriptor, 4000));
    assert(vfs::write(descriptor, more.data(), more.size()).transferred == more.size());
    vfs::close(descriptor);
    assert(read_all("/first.bin") == first + more);
    assert(read_all("/second.bin") == second);
}

void test_truncate() {
    write_file("/cut.bin", pattern(9000, 4));
    const int descriptor = vfs::open("/cut.bin", read_write);
    assert(vfs::truncate(descriptor, 100) == io::Error::None);
    vfs::close(descriptor);
    assert(read_all("/cut.bin") == pattern(9000, 4).substr(0, 100));

    // 補長：多出來的部分是零。 / Extending: the new part is zeros.
    const int longer = vfs::open("/cut.bin", read_write);
    assert(vfs::truncate(longer, 6000) == io::Error::None);
    vfs::close(longer);
    const auto content = read_all("/cut.bin");
    assert(content.size() == 6000 && content.substr(100) == std::string(5900, '\0'));

    // 以 Truncate 開啟：清成空的。 / Opened with Truncate: emptied.
    write_file("/cut.bin", "short", create | vfs::OpenFlags::Truncate);
    assert(read_all("/cut.bin") == "short");
}

void test_rename_and_remove() {
    assert(vfs::rename("/new.txt", "/made/moved.txt") == io::Error::None);
    assert(!exists("/new.txt") && read_all("/made/moved.txt") == "hello");
    assert(vfs::rename("/made/moved.txt", "/made/inner.txt") == io::Error::AlreadyExists);
    assert(vfs::rename("/nothing", "/x") == io::Error::NotFound);

    // 搬整個目錄；裡面的檔案跟著走。 / Move a whole directory; its files go
    // with it.
    assert(vfs::rename("/made", "/renamed") == io::Error::None);
    assert(!exists("/made") && read_all("/renamed/inner.txt") == "inside");
    assert(read_all("/renamed/file-42.txt") == "/made/file-42.txt");
    // 目錄不能搬進自己底下。 / A directory cannot move inside itself.
    assert(vfs::rename("/renamed", "/renamed/deeper") == io::Error::InvalidArgument);

    // 開著的檔案不能刪也不能搬。 / An open file can be neither deleted nor
    // moved.
    const int open_file = vfs::open("/renamed/inner.txt");
    assert(vfs::remove("/renamed/inner.txt") == io::Error::Busy);
    assert(vfs::rename("/renamed", "/elsewhere") == io::Error::Busy);
    vfs::close(open_file);

    assert(vfs::remove("/renamed") == io::Error::NotEmpty);
    assert(vfs::remove("/renamed/inner.txt") == io::Error::None);
    assert(!exists("/renamed/inner.txt"));
    assert(vfs::remove("/renamed/inner.txt") == io::Error::NotFound);
    assert(vfs::make_directory("/empty") == io::Error::None);
    assert(vfs::remove("/empty") == io::Error::None && !exists("/empty"));

    // 刪掉原本映像裡的檔案也一樣。 / Deleting a file from the original image
    // works the same.
    assert(vfs::remove("/big.bin") == io::Error::None);
    assert(read_all("/README.TXT") == "Hello from exFAT!\n");
}

void test_running_out_of_space() {
    // 一直寫到裝置滿為止，然後刪掉：空間必須回來。
    // Write until the device is full, then delete: the space has to come back.
    const int descriptor = vfs::open("/fill.bin", create);
    assert(descriptor >= 0);
    const auto chunk = pattern(4096, 9);
    std::size_t written = 0;
    for (;;) {
        const auto result = vfs::write(descriptor, chunk.data(), chunk.size());
        if (!result) {
            assert(result.error == io::Error::NoSpace);
            break;
        }
        written += result.transferred;
    }
    vfs::close(descriptor);
    assert(written > 100 * 1024);
    // 建立新檔案只需要目錄項目，不一定還需要叢集；兩種結果都合理，但描述子要
    // 關掉。
    // Creating a file needs only directory entries, not necessarily a cluster,
    // so either outcome is fair — but the descriptor has to be closed.
    const int another = vfs::open("/full.txt", create);
    assert(another == vfs::error_no_space || another >= 0);
    if (another >= 0) vfs::close(another);
    vfs::remove("/full.txt");
    assert(vfs::remove("/fill.bin") == io::Error::None);
    write_file("/after.txt", pattern(50000, 5));
    assert(read_all("/after.txt") == pattern(50000, 5));
}

} // namespace

int main(int argc, char** argv) {
    assert(argc >= 3 && "usage: exfat_write_smoke <exfat-clean.img> <output.img>");
    heap::initialize(arena, sizeof(arena));
    std::vector<std::uint8_t> image;
    {
        std::ifstream file(argv[1], std::ios::binary);
        assert(file && "cannot open the test image");
        image.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    io::RamDisk disk(image.data(), image.size());
    io::BlockCache cache(disk);
    assert(cache.initialize(16));
    exfat::Volume volume;
    assert(volume.mount(cache, true) == exfat::MountError::None && !volume.read_only());
    // VolumeDirty 在任何修改之前就已經寫到裝置上。 / VolumeDirty reached the
    // device before any modification.
    assert((image[106] & 0x02) != 0);

    vfs::initialize();
    assert(vfs::mount("/", volume));
    test_create_and_write();
    test_directories();
    test_fragmentation();
    test_truncate();
    test_rename_and_remove();
    test_running_out_of_space();
    assert(vfs::sync() == io::Error::None);
    assert(vfs::unmount("/"));
    volume.unmount();
    assert(cache.shutdown());
    assert((image[106] & 0x02) == 0);

    // 重新掛載（唯讀）：寫進去的東西都還在。 / Mount again (read-only):
    // everything written is still there.
    exfat::Volume again;
    assert(again.mount(disk) == exfat::MountError::None && again.read_only());
    vfs::initialize();
    assert(vfs::mount("/", again));
    assert(read_all("/renamed/moved.txt") == "hello");
    assert(read_all("/first.bin") == pattern(4000, 1) + pattern(9000, 3));
    assert(read_all("/after.txt") == pattern(50000, 5));
    assert(vfs::open("/renamed/moved.txt", vfs::OpenFlags::Write) == vfs::error_read_only);
    vfs::unmount("/");
    again.unmount();

    // 上次沒有乾淨卸載的卷冊只能唯讀掛載。 / A volume not cleanly unmounted
    // last time mounts read-only.
    auto dirty = image;
    dirty[106] |= 0x02;
    io::RamDisk dirty_disk(dirty.data(), dirty.size());
    exfat::Volume cautious;
    assert(cautious.mount(dirty_disk, true) == exfat::MountError::None && cautious.read_only());
    cautious.unmount();
    assert((dirty[106] & 0x02) != 0);

    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    assert(heap::available() == heap::capacity() && heap::consistent());
    return 0;
}
