#include "shirley/rootfs.hpp"

#include "shirley/fs.hpp"
#include "shirley/ram_disk.hpp"

namespace shirley::fs {

bool mount_rootfs() {
    // RAM disk 必須活得比掛載久，因為檔案系統只保留指標；區域 static 讓它的
    // 生命週期和核心一樣長，又不必為了一個物件動用全域建構子。
    //
    // The RAM disk has to outlive the mount, because the file system keeps
    // only a pointer to it. A function-local static gives it the kernel's own
    // lifetime without dragging in global constructors for a single object.
    static io::RamDisk disk(rootfs_image(), rootfs_image_size());
    return mount(disk);
}

} // namespace shirley::fs
