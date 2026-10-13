#pragma once

#include <cstddef>

// 把區塊裝置上的檔案系統掛進 VFS。根檔案系統與 devfs 在開機時就掛好了；這裡
// 處理的是之後才掛上來的卷冊，例如一顆 exFAT 隨身碟：
//
//     /dev/<name> → device::Device → io::BlockCache → exfat::Volume → vfs
//
// 每個掛載都有自己的區塊快取與檔案系統實例，放在一個固定大小的表裡；緩衝區
// 與大寫表從堆積配置，卸載時還回去。目前認得的格式只有 exFAT，而且唯讀。
//
// Mount a file system living on a block device into the VFS. The root file
// system and devfs are mounted at boot; this handles volumes mounted later,
// such as an exFAT USB stick:
//
//     /dev/<name> → device::Device → io::BlockCache → exfat::Volume → vfs
//
// Each mount has its own block cache and file system instance, kept in a
// fixed-size table; the buffers and the up-case table come from the heap and
// go back to it on unmount. The only format recognized today is exFAT, and it
// is read-only.
namespace shirley::fs {

// 同時可以掛載的裝置數。 / How many devices can be mounted at once.
constexpr std::size_t max_device_mounts = 2;

// 把 device（例如 "/dev/sdb1"）上的檔案系統掛在 path。兩個路徑都可以是相對
// 路徑，相對於 base 解析。成功時回傳 nullptr，失敗時回傳說明原因的文字。
//
// Mount the file system on device (such as "/dev/sdb1") at path. Either path
// may be relative, resolved against base. Returns nullptr on success, or text
// saying why it failed.
const char* mount_device(const char* device, const char* path, const char* base = nullptr);

// 卸載 mount_device() 掛上的檔案系統；同樣以 nullptr 表示成功。開機時掛的根
// 檔案系統與 devfs 不歸這裡管，因此會被拒絕。
//
// Unmount a file system mount_device() mounted, again returning nullptr on
// success. The root file system and devfs mounted at boot are not this code's
// to remove, so they are refused.
const char* unmount_device(const char* path, const char* base = nullptr);

} // namespace shirley::fs
