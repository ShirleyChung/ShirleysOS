#include "shirley/device.hpp"
#include "shirley/text.hpp"
#include "shirley/vfs.hpp"

// devfs：把裝置註冊表當成一個目錄呈現出來。
//
// 這個檔案系統沒有自己的儲存空間，也不持有任何裝置狀態：它的內容就是註冊表
// 目前的內容，因此 `/dev/kbd0` 與 `device::find("kbd0")` 指的是同一個物件，
// 兩者之間不可能不一致。這也是 `/dev` 是命名空間而不是驅動程式的意思。
//
// devfs: the device registry presented as a directory.
//
// This file system has no storage and holds no device state; its content is
// whatever the registry holds right now, so `/dev/kbd0` and
// `device::find("kbd0")` are the very same object and the two can never
// disagree. That is what it means to say `/dev` is a namespace rather than a
// driver.
namespace shirley::vfs {
namespace {

// 讀寫區塊裝置的位元組時借用的一塊緩衝區。一個區塊讀進來、取出需要的那一段
// 再丟掉，因此位元組層的存取不必對齊到區塊邊界。
//
// 只有一塊，而且只在一般核心程式中使用：中斷處理常式不會碰檔案系統。
//
// The scratch a byte-level access to a block device borrows. One block is read
// in, the wanted slice taken out of it, and the rest discarded, so byte access
// need not be block-aligned.
//
// There is one of these, and only ordinary kernel code uses it: an interrupt
// handler never touches a file system.
constexpr std::size_t max_block_bytes = 4096;
unsigned char block_scratch[max_block_bytes];

Type type_of(device::Device& device) {
    return device.is_block_device() ? Type::BlockDevice : Type::CharacterDevice;
}

std::uint64_t size_of(device::Device& device) {
    // 區塊裝置的大小是容量。字元裝置沒有長度可言——鍵盤有多大是沒有意義的
    // 問題——因此回報 0。
    //
    // A block device's size is its capacity. A character device has no length
    // to speak of — how big a keyboard is, is not a question — so it reports
    // zero.
    if (!device.is_block_device()) return 0;
    return device.block_count() * static_cast<std::uint64_t>(device.block_size());
}

Node node_for(device::Device& device) {
    Node node;
    node.id = 0;
    node.device = &device;
    node.type = type_of(device);
    node.size = size_of(device);
    text::copy(node.name, sizeof(node.name), device.name);
    return node;
}

// 以位元組定址讀一個區塊裝置。offset 與 length 都不必對齊：起點所在的那一塊
// 先整塊讀進來，再從裡面取出要的部分。
//
// Read a block device addressed in bytes. Neither offset nor length has to be
// aligned: the block the range starts in is read whole, and the wanted part is
// taken out of it.
io::Result read_blocks_as_bytes(device::Device& device, std::uint64_t offset, void* buffer,
                                std::size_t length) {
    const auto block_bytes = device.block_size();
    if (block_bytes == 0 || block_bytes > max_block_bytes) return {0, io::Error::Unsupported};
    const auto capacity = device.block_count() * static_cast<std::uint64_t>(block_bytes);
    if (offset >= capacity) return {0, io::Error::None};
    if (length > capacity - offset) length = static_cast<std::size_t>(capacity - offset);

    auto* out = static_cast<unsigned char*>(buffer);
    std::size_t moved = 0;
    while (moved < length) {
        const auto position = offset + moved;
        const auto block = position / block_bytes;
        const auto inside = static_cast<std::size_t>(position % block_bytes);
        const auto remaining = block_bytes - inside;
        const auto amount = length - moved < remaining ? length - moved : remaining;
        const auto result = device.block_read(block, 1, block_scratch);
        if (!result) return {moved, result.error};
        for (std::size_t i = 0; i < amount; ++i) out[moved + i] = block_scratch[inside + i];
        moved += amount;
    }
    return {moved, io::Error::None};
}

// 對應的寫入。只改到一部分的那一塊必須先讀回來再寫回去，否則同一塊裡其他的
// 位元組會被清成零。
//
// The matching write. A block only partly overwritten has to be read back
// first and then written whole, or the other bytes in it would be zeroed.
io::Result write_blocks_as_bytes(device::Device& device, std::uint64_t offset,
                                 const void* buffer, std::size_t length) {
    const auto block_bytes = device.block_size();
    if (block_bytes == 0 || block_bytes > max_block_bytes) return {0, io::Error::Unsupported};
    const auto capacity = device.block_count() * static_cast<std::uint64_t>(block_bytes);
    if (offset >= capacity) return {0, io::Error::OutOfRange};
    if (length > capacity - offset) length = static_cast<std::size_t>(capacity - offset);

    const auto* in = static_cast<const unsigned char*>(buffer);
    std::size_t moved = 0;
    while (moved < length) {
        const auto position = offset + moved;
        const auto block = position / block_bytes;
        const auto inside = static_cast<std::size_t>(position % block_bytes);
        const auto remaining = block_bytes - inside;
        const auto amount = length - moved < remaining ? length - moved : remaining;
        if (amount != block_bytes) {
            const auto existing = device.block_read(block, 1, block_scratch);
            if (!existing) return {moved, existing.error};
        }
        for (std::size_t i = 0; i < amount; ++i) block_scratch[inside + i] = in[moved + i];
        const auto result = device.block_write(block, 1, block_scratch);
        if (!result) return {moved, result.error};
        moved += amount;
    }
    return {moved, io::Error::None};
}

class DeviceFileSystem final : public FileSystem {
public:
    const char* name() const override { return "devfs"; }

    // devfs 只有一層：根目錄，底下全部是裝置。裝置沒有階層，硬造一層出來只會
    // 多一個要維護的東西。
    //
    // devfs is one level deep: a root, and devices in it. Devices have no
    // hierarchy, and inventing one would only be another thing to maintain.
    bool root(Node& node) override {
        node = Node{};
        node.type = Type::Directory;
        return true;
    }

    bool lookup(const Node& directory, const char* component, Node& result) override {
        if (!directory.directory()) return false;
        auto* device = device::find(component);
        if (device == nullptr) return false;
        result = node_for(*device);
        return true;
    }

    // 依註冊表目前的順序列出。索引不是穩定的識別碼——移除一個裝置會讓最後一個
    // 補上它的位置——但目前沒有裝置在開機後才離開，而查詢一律以名字為準。
    //
    // Listed in the registry's current order. An index is not a stable
    // identifier, since removing a device moves the last one into its slot, but
    // no device leaves after boot today and every lookup goes by name anyway.
    bool list(const Node& directory, std::size_t position, Node& child) override {
        if (!directory.directory()) return false;
        auto* device = device::at(position);
        if (device == nullptr) return false;
        child = node_for(*device);
        return true;
    }

    io::Result read(const Node& file, std::uint64_t offset, void* buffer,
                    std::size_t length) override {
        if (file.device == nullptr) return {0, io::Error::InvalidArgument};
        if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
        if (file.type == Type::BlockDevice)
            return read_blocks_as_bytes(*file.device, offset, buffer, length);
        // 字元裝置沒有位置：讀到的永遠是「現在有什麼」。offset 因此被忽略，
        // 而不是被當成錯誤——是呼叫端的檔案位置在前進，不是裝置的。
        //
        // A character device has no position; a read always yields whatever is
        // there now. The offset is therefore ignored rather than treated as an
        // error: it is the caller's file position that advances, not the
        // device's.
        return file.device->read(buffer, length);
    }

    io::Result write(const Node& file, std::uint64_t offset, const void* buffer,
                     std::size_t length) override {
        if (file.device == nullptr) return {0, io::Error::InvalidArgument};
        if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
        if (file.type == Type::BlockDevice)
            return write_blocks_as_bytes(*file.device, offset, buffer, length);
        return file.device->write(buffer, length);
    }

    // 裝置寫不寫得進去是每個裝置自己的事：`null` 可以，`kbd0` 不行，兩者都在
    // devfs 底下。
    //
    // 判斷的方法是寫入 0 個位元組。光看操作表裡有沒有 write 是不夠的：kbd0
    // 用的是通用的 ByteStream 轉接表，那張表有 write，但它背後的輸入佇列一律
    // 拒絕寫入。而寫 0 個位元組依照 io::Result 的約定不搬動任何東西，因此問
    // 這個問題不會有副作用——它問的正好就是「這個裝置接受寫入嗎」。
    //
    // Whether a device can be written belongs to the device: `null` can be and
    // `kbd0` cannot, and both live under devfs.
    //
    // The way to find out is to write zero bytes. Looking for a write in the
    // operation table is not enough: kbd0 uses the generic ByteStream adapter
    // table, which has one, while the input queue behind it refuses every
    // write. A write of zero bytes moves nothing by the io::Result contract, so
    // asking has no side effect — and it asks exactly the right question.
    bool writable(const Node& node) const override {
        if (node.device == nullptr) return false;
        if (node.type == Type::BlockDevice)
            return node.device->operations != nullptr &&
                   node.device->operations->block_write != nullptr;
        return static_cast<bool>(node.device->write(nullptr, 0));
    }
};

constinit DeviceFileSystem device_filesystem;

} // namespace

FileSystem& devfs() { return device_filesystem; }

} // namespace shirley::vfs
