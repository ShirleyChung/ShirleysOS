#include "shirley/device.hpp"

#include "shirley/block_device.hpp"
#include "shirley/console.hpp"
#include "shirley/text.hpp"

namespace shirley::device {
namespace {

// 註冊表就是一張指標表。裝置物件屬於驅動程式，這裡只記得它們在哪裡，因此
// 註冊與移除都不會搬動任何裝置狀態。
//
// The registry is a table of pointers. A device object belongs to its driver
// and this only remembers where they are, so neither registering nor removing
// moves any device state.
Device* devices[max_devices];
std::size_t registered = 0;

// 驅動程式的裝置物件都是靜態物件，而核心不執行 .init_array：需要執行期建構
// 的靜態物件永遠不會被建構，只會維持 .bss 的全零狀態，於是每一個裝置都會
// 帶著空名字被註冊表拒絕——而且是在開機時安靜地發生。這個 static_assert 讓
// 那個錯誤變成建置失敗：只要 Device 的建構子不再能在編譯期完成，這裡就過不去。
//
// A driver's device object is a static one, and the kernel does not run
// .init_array: a static needing run-time construction is never constructed at
// all and keeps the zeroes .bss gave it, so every device would reach the
// registry with an empty name and be refused — quietly, at boot. This
// static_assert turns that into a build failure: the moment Device's
// constructor stops being usable at compile time, this stops compiling.
constexpr Operations constant_initialization_probe{};
constexpr Device constant_initialization_check{"probe", Type::Character,
                                               constant_initialization_probe};
static_assert(constant_initialization_check.name[0] == 'p',
              "Device 必須能在編譯期初始化 / Device must be constant-initializable");

// 名稱必須非空且放得下，否則兩個裝置可能因為截斷而看起來同名。
// A name has to be non-empty and has to fit, or two devices could look alike
// after truncation.
bool valid_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    return text::length(name) <= max_name_length;
}

std::size_t index_of(const Device& device) {
    for (std::size_t i = 0; i < registered; ++i) {
        if (devices[i] == &device) return i;
    }
    return max_devices;
}

} // namespace

io::Result Device::read(void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (operations == nullptr || operations->read == nullptr) return {0, io::Error::Unsupported};
    return operations->read(*this, buffer, length);
}

io::Result Device::write(const void* buffer, std::size_t length) {
    if (length != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (operations == nullptr || operations->write == nullptr) return {0, io::Error::Unsupported};
    return operations->write(*this, buffer, length);
}

// open 與 close 沿用 POSIX 的慣例：0 是成功，負數是錯誤。沒有提供這個操作的
// 裝置不需要準備或收尾，因此當成成功而不是錯誤。
//
// open and close follow the POSIX convention of zero for success and a
// negative value for failure. A device that supplies neither needs no
// preparation or teardown, so its absence is success rather than an error.
int Device::open() {
    if (operations == nullptr || operations->open == nullptr) return 0;
    return operations->open(*this);
}

int Device::close() {
    if (operations == nullptr || operations->close == nullptr) return 0;
    return operations->close(*this);
}

// 相對地，沒有提供 control 的裝置就是不認得任何命令，這是錯誤而不是成功。
// A device with no control, in contrast, recognizes no request at all, and
// that is an error rather than success.
int Device::control(unsigned long request, void* argument) {
    if (operations == nullptr || operations->control == nullptr) return -1;
    return operations->control(*this, request, argument);
}

io::Result Device::block_read(std::uint64_t first, std::size_t count, void* buffer) {
    if (count != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (operations == nullptr || operations->block_read == nullptr)
        return {0, io::Error::Unsupported};
    return operations->block_read(*this, first, count, buffer);
}

io::Result Device::block_write(std::uint64_t first, std::size_t count, const void* buffer) {
    if (count != 0 && buffer == nullptr) return {0, io::Error::InvalidArgument};
    if (operations == nullptr || operations->block_write == nullptr)
        return {0, io::Error::Unsupported};
    return operations->block_write(*this, first, count, buffer);
}

std::size_t Device::block_size() {
    if (operations == nullptr || operations->block_size == nullptr) return 0;
    return operations->block_size(*this);
}

std::uint64_t Device::block_count() {
    if (operations == nullptr || operations->block_count == nullptr) return 0;
    return operations->block_count(*this);
}

// 能定址區塊才算區塊裝置。以能力判斷而不是以 Type 判斷：Type 是給人看的
// 分類，實際能做什麼一律由操作表決定。
//
// What makes a device a block device is being able to address blocks. This
// tests the capability rather than the Type, because Type is a label for
// people while what a device can do is always decided by its operation table.
bool Device::is_block_device() { return block_size() != 0 && block_count() != 0; }

const char* type_name(Type type) {
    switch (type) {
    case Type::Character: return "char";
    case Type::Block: return "block";
    case Type::Input: return "input";
    case Type::Network: return "net";
    }
    return "unknown";
}

void initialize() {
    for (std::size_t i = 0; i < max_devices; ++i) devices[i] = nullptr;
    registered = 0;
}

Status register_device(Device& device) {
    if (!valid_name(device.name)) return Status::InvalidArgument;
    // 沒有操作表的裝置註冊了也沒有用：任何讀寫都只會回報 Unsupported，而它
    // 佔走的名字會擋住之後真正能用的驅動程式。
    //
    // A device with no operation table would be useless in the registry: every
    // read and write reports Unsupported, and the name it occupies blocks the
    // driver that could really serve it later.
    if (device.operations == nullptr) return Status::InvalidArgument;
    if (registered >= max_devices) return Status::RegistryFull;
    // 名稱是唯一的鍵值。重複註冊同一個物件也算重名，這通常代表驅動程式被
    // 初始化了兩次，安靜接受只會讓查詢結果變得無法預測。
    //
    // The name is the one key. Registering the same object twice counts as a
    // duplicate too; that normally means a driver was initialized twice, and
    // accepting it quietly would only make lookups unpredictable.
    if (find(device.name) != nullptr) return Status::DuplicateName;
    devices[registered++] = &device;
    return Status::Ok;
}

Status unregister_device(Device& device) {
    const auto index = index_of(device);
    if (index == max_devices) return Status::NotRegistered;
    // 把最後一個移過來填洞，表格因此永遠是連續的，列舉不必跳過空位。
    // The last entry fills the hole, which keeps the table dense so
    // enumeration never has to skip empty slots.
    devices[index] = devices[registered - 1];
    devices[--registered] = nullptr;
    return Status::Ok;
}

Device* find(const char* name) {
    if (name == nullptr) return nullptr;
    for (std::size_t i = 0; i < registered; ++i) {
        if (text::equals(devices[i]->name, name)) return devices[i];
    }
    return nullptr;
}

std::size_t count() { return registered; }

Device* at(std::size_t index) { return index < registered ? devices[index] : nullptr; }

void dump() {
    for (std::size_t i = 0; i < registered; ++i) {
        console::write("[device] ");
        console::write(devices[i]->name);
        console::write(" type=");
        console::write(type_name(devices[i]->type));
        console::write("\n");
    }
}

namespace {

// ByteStream 後端的操作表。driver_data 指向串流；串流不見了就當成不支援，
// 這比對空指標呼叫虛擬函式安全得多。
//
// The operation table for a byte stream backend. driver_data points at the
// stream, and a missing stream is reported as unsupported, which is a great
// deal safer than calling a virtual function through a null pointer.
io::ByteStream* stream_of(Device& device) {
    return static_cast<io::ByteStream*>(device.driver_data);
}

io::Result stream_read(Device& device, void* buffer, std::size_t length) {
    auto* stream = stream_of(device);
    if (stream == nullptr) return {0, io::Error::Unsupported};
    return stream->read(buffer, length);
}

io::Result stream_write(Device& device, const void* buffer, std::size_t length) {
    auto* stream = stream_of(device);
    if (stream == nullptr) return {0, io::Error::Unsupported};
    return stream->write(buffer, length);
}

// io::BlockDevice 後端的操作表；driver_data 指向那個區塊裝置。
// The operation table for an io::BlockDevice backend, with driver_data
// pointing at that block device.
io::BlockDevice* block_device_of(Device& device) {
    return static_cast<io::BlockDevice*>(device.driver_data);
}

io::Result device_block_read(Device& device, std::uint64_t first, std::size_t count,
                             void* buffer) {
    auto* disk = block_device_of(device);
    if (disk == nullptr) return {0, io::Error::Unsupported};
    return disk->read_blocks(first, count, buffer);
}

io::Result device_block_write(Device& device, std::uint64_t first, std::size_t count,
                              const void* buffer) {
    auto* disk = block_device_of(device);
    if (disk == nullptr) return {0, io::Error::Unsupported};
    return disk->write_blocks(first, count, buffer);
}

std::size_t device_block_size(Device& device) {
    auto* disk = block_device_of(device);
    return disk == nullptr ? 0 : disk->block_size();
}

std::uint64_t device_block_count(Device& device) {
    auto* disk = block_device_of(device);
    return disk == nullptr ? 0 : disk->block_count();
}

} // namespace

extern const Operations stream_operations{nullptr, nullptr, stream_read, stream_write, nullptr};

extern const Operations block_operations{nullptr,           nullptr,
                                         nullptr,           nullptr,
                                         nullptr,           device_block_read,
                                         device_block_write, device_block_size,
                                         device_block_count};

io::Result Stream::read(void* buffer, std::size_t length) {
    if (device_ == nullptr) return {0, io::Error::Unsupported};
    return device_->read(buffer, length);
}

io::Result Stream::write(const void* buffer, std::size_t length) {
    if (device_ == nullptr) return {0, io::Error::Unsupported};
    return device_->write(buffer, length);
}

} // namespace shirley::device
