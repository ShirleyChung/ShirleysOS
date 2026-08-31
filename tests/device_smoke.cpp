#include "shirley/console.hpp"
#include "shirley/device.hpp"
#include "shirley/input_queue.hpp"
#include "shirley/io.hpp"

#include <cassert>
#include <cstring>

namespace {

using shirley::device::Device;
using shirley::device::Operations;
using shirley::device::Status;
using shirley::device::Type;

// 一個什麼都不做的操作表就足以註冊：註冊表關心的是名字，不是行為。
// A table that does nothing is enough to register with: the registry cares
// about the name, not about behaviour.
constexpr Operations empty_operations{};

// 註冊、查詢、移除，以及移除之後就查不到了。
// Register, look up, remove, and no longer find it afterwards.
void test_register_and_find() {
    shirley::device::initialize();
    assert(shirley::device::count() == 0);
    assert(shirley::device::find("kbd0") == nullptr);

    Device keyboard{"kbd0", Type::Input, empty_operations};
    assert(shirley::device::register_device(keyboard) == Status::Ok);
    assert(shirley::device::count() == 1);

    Device* found = shirley::device::find("kbd0");
    assert(found == &keyboard);
    assert(found->type == Type::Input);
    assert(std::strcmp(found->name, "kbd0") == 0);

    assert(shirley::device::unregister_device(keyboard) == Status::Ok);
    assert(shirley::device::count() == 0);
    assert(shirley::device::find("kbd0") == nullptr);
    // 移除兩次是呼叫端的錯，不是靜默的無作用。
    // Removing twice is the caller's mistake rather than a silent no-op.
    assert(shirley::device::unregister_device(keyboard) == Status::NotRegistered);
}

// 名字是唯一的鍵值。撞名通常代表某個驅動程式被初始化了兩次，接受它只會讓
// 查詢結果變得無法預測。
//
// The name is the one key. A collision normally means a driver was initialized
// twice, and accepting it would only make lookups unpredictable.
void test_duplicate_name() {
    shirley::device::initialize();
    Device first{"uart0", Type::Character, empty_operations};
    Device second{"uart0", Type::Character, empty_operations};
    assert(shirley::device::register_device(first) == Status::Ok);
    assert(shirley::device::register_device(second) == Status::DuplicateName);
    // 同一個物件註冊兩次也一樣被擋下來。
    // Registering the same object twice is refused in the same way.
    assert(shirley::device::register_device(first) == Status::DuplicateName);
    assert(shirley::device::count() == 1);
    assert(shirley::device::find("uart0") == &first);
}

// 名稱為空、過長，或沒有操作表的裝置都不能進註冊表：它們佔走名字卻什麼也
// 提供不了。過長的名字會在建構時就變成空字串，不會安靜地被截斷成別人。
//
// A device with an empty or over-long name, or with no operation table, must
// stay out: it would take a name and supply nothing. An over-long name becomes
// empty at construction rather than being quietly truncated into someone else.
void test_invalid_devices() {
    shirley::device::initialize();
    Device unnamed{"", Type::Character, empty_operations};
    assert(shirley::device::register_device(unnamed) == Status::InvalidArgument);

    Device oversized{"0123456789012345678901234567890123456789", Type::Character,
                     empty_operations};
    assert(oversized.name[0] == '\0');
    assert(shirley::device::register_device(oversized) == Status::InvalidArgument);

    Device without_operations{"ghost", Type::Character, empty_operations};
    without_operations.operations = nullptr;
    assert(shirley::device::register_device(without_operations) == Status::InvalidArgument);

    assert(shirley::device::count() == 0);
    assert(shirley::device::find("") == nullptr);
    assert(shirley::device::find(nullptr) == nullptr);
}

// 表滿了要明確拒絕，不能覆寫既有項目。移除一個之後空位要能再被使用。
// A full table refuses plainly rather than overwriting an entry, and freeing
// one slot makes room again.
void test_registry_full() {
    shirley::device::initialize();
    static Device devices[shirley::device::max_devices];
    for (std::size_t index = 0; index < shirley::device::max_devices; ++index) {
        char name[8] = "dev";
        name[3] = static_cast<char>('a' + index / 26);
        name[4] = static_cast<char>('a' + index % 26);
        name[5] = '\0';
        devices[index] = Device{name, Type::Block, empty_operations};
        assert(shirley::device::register_device(devices[index]) == Status::Ok);
    }
    assert(shirley::device::count() == shirley::device::max_devices);

    Device extra{"overflow", Type::Block, empty_operations};
    assert(shirley::device::register_device(extra) == Status::RegistryFull);
    assert(shirley::device::count() == shirley::device::max_devices);

    // 移除中間的一個，剩下的每一個都還要找得到——空洞是由最後一個填上的。
    // Remove one from the middle; every other device still has to be findable,
    // because the hole is filled by the last entry.
    assert(shirley::device::unregister_device(devices[0]) == Status::Ok);
    assert(shirley::device::register_device(extra) == Status::Ok);
    for (std::size_t index = 1; index < shirley::device::max_devices; ++index) {
        assert(shirley::device::find(devices[index].name) == &devices[index]);
    }
    assert(shirley::device::find("overflow") == &extra);
}

// 沒有提供的操作要回報 Unsupported，而不是解參考空指標；open 與 close 例外，
// 不需要準備或收尾的裝置視為成功。
//
// An operation that was not supplied reports Unsupported instead of
// dereferencing a null pointer. open and close are the exception: a device
// needing neither preparation nor teardown counts as success.
void test_missing_operations() {
    Device bare{"bare", Type::Character, empty_operations};
    char buffer[4] = {};
    auto result = bare.read(buffer, sizeof(buffer));
    assert(!result);
    assert(result.error == shirley::io::Error::Unsupported);
    result = bare.write(buffer, sizeof(buffer));
    assert(!result);
    assert(result.error == shirley::io::Error::Unsupported);
    assert(bare.open() == 0);
    assert(bare.close() == 0);
    assert(bare.control(0, nullptr) != 0);
    // 空緩衝區是參數錯誤，即使裝置本來就不支援讀取。
    // A null buffer is an argument error even on a device that cannot read.
    result = bare.read(nullptr, 4);
    assert(result.error == shirley::io::Error::InvalidArgument);
}

// 中斷輸入佇列本來就是位元組串流，因此變成裝置不需要寫任何讀取邏輯。
// 這正是 kbd0 的組成方式。
//
// The interrupt input queue is a byte stream already, so becoming a device
// takes no read logic at all. This is exactly how kbd0 is put together.
void test_stream_backed_device() {
    shirley::io::InputQueue queue;
    Device keyboard{"kbd0", Type::Input, shirley::device::stream_operations, &queue};
    assert(queue.push('h'));
    assert(queue.push('i'));

    char buffer[8] = {};
    // 使用端也可以直接走操作表，這是驅動程式與使用者之間唯一的約定。
    // A consumer may go through the operation table directly; that table is
    // the whole of the contract between a driver and its user.
    auto result = keyboard.operations->read(keyboard, buffer, sizeof(buffer));
    assert(result);
    assert(result.transferred == 2);
    assert(buffer[0] == 'h' && buffer[1] == 'i');

    // 輸入佇列不能寫入，因此包成裝置之後也不能。
    // The queue cannot be written to, and wrapping it as a device does not
    // change that.
    result = keyboard.write("x", 1);
    assert(!result);
    assert(result.error == shirley::io::Error::Unsupported);

    // 空的來源傳輸 0 個位元組，那不是錯誤：沒有人在打字不是故障。
    // An empty source transfers zero bytes, which is not an error: nobody
    // typing is not a fault.
    result = keyboard.read(buffer, sizeof(buffer));
    assert(result);
    assert(result.transferred == 0);
}

// 反方向的轉接：裝置可以當成位元組串流使用，未來的 VFS 節點就是這樣消費它。
// The other direction: a device can be used as a byte stream, which is how a
// future VFS node will consume one.
void test_device_as_stream() {
    shirley::io::InputQueue queue;
    Device keyboard{"kbd0", Type::Input, shirley::device::stream_operations, &queue};
    shirley::device::Stream stream{&keyboard};
    assert(queue.push('k'));

    char value = '\0';
    const auto result = stream.read(&value, 1);
    assert(result);
    assert(value == 'k');

    // 未繫結的串流是不支援，不是「沒有資料」。
    // An unbound stream is unsupported rather than merely empty.
    shirley::device::Stream unbound;
    assert(unbound.read(&value, 1).error == shirley::io::Error::Unsupported);
    assert(unbound.write(&value, 1).error == shirley::io::Error::Unsupported);
}

// null 裝置：讀是檔案結尾，寫全部接受並丟棄。回報寫入 0 個位元組會讓呼叫端
// 以為裝置滿了，因此必須回報完整長度。
//
// The null device: a read is end of file and a write accepts and discards
// everything. Reporting zero bytes written would make a caller believe the
// device is full, so the full length has to be reported.
void test_null_device() {
    shirley::device::initialize();
    assert(shirley::device::null_initialize());
    Device* null = shirley::device::find("null");
    assert(null != nullptr);
    assert(null->type == Type::Character);

    char buffer[4] = {'a', 'b', 'c', 'd'};
    auto result = null->read(buffer, sizeof(buffer));
    assert(result);
    assert(result.transferred == 0);
    assert(buffer[0] == 'a');

    result = null->write(buffer, sizeof(buffer));
    assert(result);
    assert(result.transferred == sizeof(buffer));
}

// 主控台把幾個輸入裝置匯成一條輸入，並且是接上第一個裝置的那一刻才把標準
// 輸入指過去——在那之前沒有任何字元可能出現。
//
// The console merges several input devices into one input, and points standard
// input at itself the moment the first device attaches; before that no
// character could possibly appear.
void test_console_input_multiplexing() {
    shirley::device::initialize();
    shirley::io::set_standard_input(nullptr);
    assert(shirley::console::input_count() == 0);

    shirley::io::InputQueue keys;
    shirley::io::InputQueue serial;
    Device keyboard{"kbd0", Type::Input, shirley::device::stream_operations, &keys};
    Device uart{"uart0", Type::Character, shirley::device::stream_operations, &serial};

    char value = '\0';
    // 還沒有輸入裝置時，主控台的讀取沒有東西可回，但也不是錯誤。
    // With no input device the console has nothing to return, and that is
    // still not an error.
    auto result = shirley::console::read(&value, 1);
    assert(result);
    assert(result.transferred == 0);

    assert(shirley::console::attach_input(keyboard));
    assert(shirley::console::input_count() == 1);
    assert(shirley::io::standard_input() == &shirley::console::input_stream());
    assert(shirley::console::attach_input(uart));
    assert(shirley::console::input_count() == 2);
    // 同一個裝置接兩次不會佔掉兩個位置。
    // Attaching the same device twice does not take two slots.
    assert(shirley::console::attach_input(keyboard));
    assert(shirley::console::input_count() == 2);

    // 兩個來源都能驅動同一條輸入，順序是接上的順序。
    // Either source drives the same input, in the order they were attached.
    assert(serial.push('s'));
    result = shirley::console::read(&value, 1);
    assert(result.transferred == 1 && value == 's');

    assert(keys.push('k'));
    result = shirley::console::read(&value, 1);
    assert(result.transferred == 1 && value == 'k');

    // 標準輸入走的就是這條路徑，這是 shell 真正讀到按鍵的方式。
    // Standard input takes this very path, which is how the shell really sees
    // a keystroke.
    assert(keys.push('!'));
    result = shirley::io::read_standard_input(&value, 1);
    assert(result.transferred == 1 && value == '!');

    // 最後一個輸入裝置離開後，標準輸入必須恢復成未設定，否則 shell 會以為
    // 這台機器還能打字。
    //
    // Once the last input device leaves, standard input has to go back to
    // unset, or the shell would believe the machine can still be typed at.
    assert(shirley::console::detach_input(keyboard));
    assert(shirley::console::detach_input(uart));
    assert(shirley::console::input_count() == 0);
    assert(shirley::io::standard_input() == nullptr);
    assert(!shirley::console::detach_input(keyboard));
}

// 主控台自己也是一個裝置，未來的 /dev/console 指的就是它。
// The console is a device in its own right, and a future /dev/console names
// exactly it.
void test_console_device() {
    shirley::device::initialize();
    auto& console = shirley::console::console_device();
    assert(std::strcmp(console.name, "console") == 0);
    assert(console.type == Type::Character);
    assert(shirley::device::register_device(console) == Status::Ok);
    assert(shirley::device::find("console") == &console);
}

// 診斷輸出用的種類名稱要穩定：開機記錄與 devices 指令都印它。
// The type names used in diagnostics have to be stable: both the boot log and
// the devices command print them.
void test_type_names() {
    assert(std::strcmp(shirley::device::type_name(Type::Character), "char") == 0);
    assert(std::strcmp(shirley::device::type_name(Type::Block), "block") == 0);
    assert(std::strcmp(shirley::device::type_name(Type::Input), "input") == 0);
    assert(std::strcmp(shirley::device::type_name(Type::Network), "net") == 0);
}

} // namespace

int main() {
    test_register_and_find();
    test_duplicate_name();
    test_invalid_devices();
    test_registry_full();
    test_missing_operations();
    test_stream_backed_device();
    test_device_as_stream();
    test_null_device();
    test_console_input_multiplexing();
    test_console_device();
    test_type_names();
    return 0;
}
