#include "shirley/rootfs.hpp"

#include "shirley/device.hpp"
#include "shirley/fs.hpp"
#include "shirley/ram_disk.hpp"

namespace shirley::fs {
namespace {

// 根檔案系統所在的區塊裝置，以 ram0 的名字公開。它是註冊表裡第一個區塊裝置，
// 之後真正的磁碟驅動程式會以同樣的方式登記，因此 `/dev/ram0` 與未來的
// `/dev/sda` 對使用端來說沒有差別。
//
// driver_data 要等 RAM disk 真的存在才填得上：那個物件在 mount_rootfs() 裡才
// 建立起來，而裝置物件必須在編譯期就初始化好（核心不執行 .init_array）。
//
// The block device the root file system lives on, published as ram0. It is the
// first block device in the registry, and a real disk driver will register the
// same way later, so `/dev/ram0` and a future `/dev/sda` look alike to whoever
// uses them.
//
// driver_data can only be filled in once the RAM disk exists: that object is
// created inside mount_rootfs(), while a device object has to be initialized at
// compile time because the kernel does not run .init_array.
constinit device::Device ram_device{"ram0", device::Type::Block, device::block_operations};

} // namespace

bool mount_rootfs() {
    // RAM disk 必須活得比掛載久，因為檔案系統只保留指標；區域 static 讓它的
    // 生命週期和核心一樣長，又不必為了一個物件動用全域建構子。
    //
    // The RAM disk has to outlive the mount, because the file system keeps
    // only a pointer to it. A function-local static gives it the kernel's own
    // lifetime without dragging in global constructors for a single object.
    static io::RamDisk disk(rootfs_image(), rootfs_image_size());
    if (!mount(disk)) return false;
    // 掛載成功之後才公開這個磁碟。順序反過來的話，註冊表裡會出現一個指向
    // 未知內容的區塊裝置。
    //
    // The disk is published only once the mount succeeded. The other order
    // would put a block device of unknown content into the registry.
    ram_device.driver_data = &disk;
    device::register_device(ram_device);
    return true;
}

device::Device& rootfs_device() { return ram_device; }

} // namespace shirley::fs
