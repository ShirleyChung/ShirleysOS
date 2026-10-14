#include "shirley/device_mount.hpp"

#include "shirley/block_cache.hpp"
#include "shirley/device.hpp"
#include "shirley/exfat.hpp"
#include "shirley/ext4.hpp"
#include "shirley/text.hpp"
#include "shirley/vfs.hpp"

namespace shirley::fs {
namespace {

// 快取的大小：64 個區塊在 512 位元組的裝置上是 32 KiB，足夠讓 FAT 與目錄
// 項目的重複讀取幾乎全部命中。
//
// The cache size: 64 blocks is 32 KiB on a 512-byte device, enough for the
// repeated reads of the FAT and directory entries to hit nearly every time.
constexpr std::size_t cache_blocks = 64;

// 每個槽位都備有每一種格式的實例，掛載時依探測結果用其中一個。實例本身很小，
// 大的緩衝區要到掛載時才從堆積配置。
//
// Each slot holds an instance of every format and a mount uses whichever the
// probe picks. The instances are small; their large buffers come from the
// heap only when mounted.
struct Slot {
    device::BlockView blocks;
    io::BlockCache cache{blocks};
    exfat::Volume exfat;
    ext4::Volume ext4;
    vfs::FileSystem* active = nullptr;
    char path[vfs::max_path_length]{};
    bool used = false;
};

// 依序認格式並掛載，回傳 nullptr 或失敗原因。
// Recognize the format and mount it, returning nullptr or why it failed.
const char* mount_volume(Slot& slot) {
    if (exfat::identify(slot.cache)) {
        // 要求可寫的掛載；卷冊上次沒有乾淨卸載、或裝置唯讀時會退回唯讀。
        // Ask for a writable mount; it falls back to read-only when the volume
        // was not cleanly unmounted last time or the device is read-only.
        const auto error = slot.exfat.mount(slot.cache, true);
        if (error != exfat::MountError::None) return exfat::mount_error_text(error);
        slot.active = &slot.exfat;
        return nullptr;
    }
    if (ext4::identify(slot.cache)) {
        const auto error = slot.ext4.mount(slot.cache);
        if (error != ext4::MountError::None) return ext4::mount_error_text(error);
        slot.active = &slot.ext4;
        return nullptr;
    }
    return "no supported file system (exFAT, ext2/3/4)";
}

void unmount_volume(Slot& slot) {
    if (slot.active == &slot.exfat) slot.exfat.unmount();
    if (slot.active == &slot.ext4) slot.ext4.unmount();
    slot.active = nullptr;
}

// constinit：核心不執行全域建構子，這張表必須在編譯期就初始化好。
// constinit: the kernel runs no global constructors, so this table has to be
// initialized at compile time.
constinit Slot slots[max_device_mounts]{};

} // namespace

const char* mount_device(const char* device_path, const char* path, const char* base) {
    if (device_path == nullptr || path == nullptr) return "needs a device and a path";
    vfs::Node node;
    if (!vfs::stat(device_path, node, base)) return "no such device";
    if (node.type != vfs::Type::BlockDevice || node.device == nullptr) return "not a block device";
    char target[vfs::max_path_length];
    if (!vfs::normalize(path, base, target, sizeof(target))) return "invalid mount point";

    Slot* slot = nullptr;
    for (auto& candidate : slots) {
        if (!candidate.used) {
            if (slot == nullptr) slot = &candidate;
        } else if (text::equals(candidate.path, target)) {
            return "already mounted there";
        }
    }
    if (slot == nullptr) return "too many mounted devices";

    slot->blocks.bind(node.device);
    if (!slot->cache.initialize(cache_blocks)) return "out of memory";
    if (const auto failure = mount_volume(*slot)) {
        slot->cache.shutdown();
        return failure;
    }
    if (!vfs::mount(target, *slot->active)) {
        unmount_volume(*slot);
        slot->cache.shutdown();
        return "the mount point is not usable";
    }
    text::copy(slot->path, sizeof(slot->path), target);
    slot->used = true;
    return nullptr;
}

const char* unmount_device(const char* path, const char* base) {
    if (path == nullptr) return "needs a path";
    char target[vfs::max_path_length];
    if (!vfs::normalize(path, base, target, sizeof(target))) return "invalid path";
    for (auto& slot : slots) {
        if (!slot.used || !text::equals(slot.path, target)) continue;
        if (!vfs::unmount(target)) return "busy";
        // exFAT 的 unmount() 寫回所有修改並清掉 VolumeDirty，快取接著把剩下的
        // 區塊寫到裝置上。寫回失敗時槽位仍然釋放——VFS 已經放手了——但要說
        // 出來；那份快取的緩衝區就留在堆積裡，不能拿來給下一個掛載用。
        //
        // exFAT's unmount() writes back every modification and clears
        // VolumeDirty, then the cache pushes whatever is left to the device. If
        // that write-back fails the slot is still released, since the VFS has
        // already let go, but the failure is reported; that cache's buffers
        // stay behind in the heap rather than being handed to the next mount.
        unmount_volume(slot);
        const bool written = static_cast<bool>(slot.cache.shutdown());
        if (!written) slot.cache = io::BlockCache{slot.blocks};
        slot.path[0] = '\0';
        slot.used = false;
        return written ? nullptr : "unmounted, but writing back to the device failed";
    }
    return "not a mounted device";
}

} // namespace shirley::fs
