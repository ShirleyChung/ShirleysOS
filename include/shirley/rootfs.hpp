#pragma once

#include "shirley/device.hpp"

#include <cstddef>

namespace shirley::fs {

// 建置時由 cmake/make-rootfs.cmake 從 rootfs/ 產生的 SHRFS1 映像。映像連結
// 在核心裡，因此開機不需要任何磁碟驅動程式就有檔案系統可掛載；換成真正的
// 磁碟之後，同一套檔案系統程式碼會直接套用在磁碟的區塊裝置上。
//
// The SHRFS1 image that cmake/make-rootfs.cmake builds from rootfs/ at build
// time. It is linked into the kernel, so boot needs no disk driver to have a
// file system to mount; once a real disk exists, the same file system code
// applies to that disk's block device unchanged.
//
// 映像可寫入，是為了讓區塊裝置介面能原封不動地套用；目前的檔案系統只讀。
// The image is writable so the block device interface applies to it unchanged;
// the file system itself is read-only today.
void* rootfs_image();
std::size_t rootfs_image_size();

// 把嵌入的映像包成 RAM disk、掛載成根檔案系統，並把那個磁碟以 ram0 的名字
// 登記到裝置註冊表；成功時回傳 true。
//
// Wrap the embedded image in a RAM disk, mount it as the root file system, and
// publish that disk in the device registry as ram0; returns true on success.
bool mount_rootfs();

// 根檔案系統所在的區塊裝置，等同於 device::find("ram0")。
// The block device the root file system lives on, the same thing
// device::find("ram0") returns.
device::Device& rootfs_device();

} // namespace shirley::fs
