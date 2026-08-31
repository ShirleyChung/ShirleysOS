#pragma once

#include "shirley/io.hpp"

#include <cstddef>
#include <cstdint>

// 裝置抽象層。硬體 → 驅動程式 → Device → 註冊表 → console，未來再接上 VFS：
//
//     PS/2 鍵盤 ──IRQ1──▶ 鍵盤驅動程式 ──▶ kbd0 ─┐
//                                                 ├──▶ console ──▶ shell
//     16550 UART ─IRQ4──▶ 序列埠驅動程式 ─▶ uart0 ┘
//
// 這一層存在的理由是把「誰在提供位元組」和「誰在使用位元組」分開。驅動程式
// 只認得自己的硬體並填好一張操作表；使用者只認得名字與那張表，因此 console
// 不知道 8259A，shell 不知道 IRQ1，而註冊表本身不知道 0x60 是什麼。
//
// 這裡刻意不做 Linux 的 driver model：沒有 bus、沒有 class、沒有 major/minor
// 編號。名字就是識別碼，一張固定大小的表就是註冊表。等到真的需要熱插拔或
// 裝置樹列舉時再談，現在多一層抽象只會多一層要維護的東西。
//
// The device abstraction layer. Hardware becomes a driver, a driver becomes a
// Device, a Device is published in the registry, and the console consumes it;
// a VFS will consume the same thing later:
//
//     PS/2 keyboard ─IRQ1─▶ keyboard driver ─▶ kbd0 ─┐
//                                                    ├─▶ console ─▶ shell
//     16550 UART ────IRQ4─▶ serial driver ───▶ uart0 ┘
//
// The layer exists to separate who produces bytes from who consumes them. A
// driver knows only its hardware and fills in an operation table; a consumer
// knows only a name and that table. So the console knows nothing of the 8259A,
// the shell knows nothing of IRQ1, and the registry itself does not know what
// port 0x60 is.
//
// This deliberately is not Linux's driver model: no buses, no classes, no
// major/minor numbers. The name is the identifier and a fixed-size table is
// the registry. That conversation is worth having when hot-plug or device-tree
// enumeration genuinely arrives; until then another layer of abstraction is
// only another layer to maintain.
namespace shirley::device {

// 裝置種類。這是給人與診斷輸出看的分類，不是行為的開關：實際能做什麼由
// 操作表決定，因此一個 Input 裝置和一個 Character 裝置的讀取路徑完全一樣。
//
// What kind of device this is. The classification is for people and for
// diagnostics rather than a switch on behaviour: what a device can actually do
// is decided by its operation table, so an Input device and a Character device
// are read through exactly the same path.
enum class Type : std::uint8_t { Character, Block, Input, Network };

// 診斷輸出用的短名稱："char"、"block"、"input"、"net"。
// The short name used in diagnostics: "char", "block", "input", "net".
const char* type_name(Type type);

struct Device;

// 驅動程式提供的操作表。每個項目都可以是空指標，代表該裝置不支援這個操作；
// 呼叫端不必自己檢查，Device 的便利函式會回報 Unsupported。
//
// 表格由驅動程式以常數持有，而不是每個裝置複製一份：同型號的十個裝置共用
// 一張表，彼此的差異放在 driver_data。
//
// The operation table a driver supplies. Any entry may be null, meaning the
// device does not support that operation; a caller need not check, because
// Device's convenience methods report Unsupported instead.
//
// A driver holds the table as a constant rather than copying it into every
// device: ten devices of one model share one table, and what differs between
// them lives in driver_data.
struct Operations {
    int (*open)(Device& device) = nullptr;
    int (*close)(Device& device) = nullptr;
    io::Result (*read)(Device& device, void* buffer, std::size_t length) = nullptr;
    io::Result (*write)(Device& device, const void* buffer, std::size_t length) = nullptr;
    // ioctl 的核心版本。命令編號目前由各驅動程式自行定義，還沒有共用的編碼
    // 規則；等到第二個驅動程式真的需要時再統一。
    //
    // The kernel's ioctl. Request numbers are defined by each driver for now;
    // there is no shared encoding yet, and none is worth inventing before a
    // second driver needs one.
    int (*control)(Device& device, unsigned long request, void* argument) = nullptr;

    // 區塊裝置專用，以區塊而不是位元組定址。磁碟本來就只能整塊讀寫，把這件事
    // 藏在位元組介面後面只會讓檔案系統無法要求它真正需要的那幾塊；VFS 讀取
    // 檔案時走上面的 read/write，需要原始磁區時走這裡。
    //
    // 非區塊裝置把這四個項目留空，於是任何區塊層存取都會回報 Unsupported。
    //
    // For block devices, addressed in blocks rather than bytes. A disk can only
    // be read and written whole blocks at a time, and hiding that behind a byte
    // interface would stop a file system from asking for exactly the blocks it
    // needs. The VFS reads a file through read/write above and reaches for raw
    // sectors through these.
    //
    // A device that is not a block device leaves all four null, and every
    // block-level access then reports Unsupported.
    io::Result (*block_read)(Device& device, std::uint64_t first, std::size_t count,
                             void* buffer) = nullptr;
    io::Result (*block_write)(Device& device, std::uint64_t first, std::size_t count,
                              const void* buffer) = nullptr;
    std::size_t (*block_size)(Device& device) = nullptr;
    std::uint64_t (*block_count)(Device& device) = nullptr;
};

// 裝置名稱的長度上限。名稱同時是註冊表的鍵值，未來也會是 /dev 底下的檔名。
// The longest device name. The name is both the registry key and, later, the
// file name under /dev.
constexpr std::size_t max_name_length = 31;

// 一個裝置。物件由驅動程式持有——通常是驅動程式檔案裡的一個 static 物件——
// 註冊表只保存指標，不配置也不釋放任何東西。核心此時還沒有動態配置器，而
// 驅動程式的生命週期本來就和它的硬體一樣長，所以這不是妥協。
//
// One device. The object is owned by its driver, normally as a static object
// inside the driver's file; the registry only stores a pointer and neither
// allocates nor frees anything. The kernel has no dynamic allocator yet, and a
// driver lives exactly as long as its hardware does, so this is not a
// compromise.
struct Device {
    Device() = default;
    // 名稱過長會變成空字串，註冊時再以 InvalidArgument 拒絕：安靜截斷的名字
    // 會指向另一個裝置，那比明確的失敗危險得多。
    //
    // 這個建構子必須是 constexpr，而且名稱要在這裡自己複製而不是呼叫
    // text::copy()。核心沒有執行 .init_array：靜態物件如果需要執行期初始化，
    // 它的建構子永遠不會被呼叫，物件就一直維持在 .bss 的全零狀態。驅動程式
    // 的裝置物件正是這種靜態物件，因此它們必須能在編譯期就完成初始化，否則
    // 每一個裝置都會帶著空名字與空操作表被註冊表拒絕。
    //
    // A name that does not fit becomes empty and is refused at registration
    // with InvalidArgument. A silently truncated name would refer to a
    // different device, which is far more dangerous than an explicit failure.
    //
    // This constructor has to be constexpr, and it has to copy the name itself
    // rather than call text::copy(). The kernel never runs .init_array: a
    // static object whose construction needs the run time is never constructed
    // at all and simply stays as the zeroes .bss gives it. A driver's device
    // object is exactly such a static, so it must be initializable at compile
    // time — otherwise every device would reach the registry with an empty
    // name and an empty operation table, and be refused.
    constexpr Device(const char* device_name, Type device_type,
                     const Operations& device_operations, void* data = nullptr)
        : type(device_type), operations(&device_operations), driver_data(data) {
        if (device_name == nullptr) return;
        std::size_t length = 0;
        while (length <= max_name_length && device_name[length] != '\0') ++length;
        if (length > max_name_length) return;
        for (std::size_t index = 0; index < length; ++index) name[index] = device_name[index];
    }

    char name[max_name_length + 1]{};
    Type type = Type::Character;
    const Operations* operations = nullptr;
    void* driver_data = nullptr;

    // 便利呼叫。驅動程式沒有提供該操作時回傳 Unsupported，而不是解參考空
    // 指標，因此使用端可以直接呼叫而不必先檢查操作表。
    //
    // Convenience calls. An operation the driver did not supply returns
    // Unsupported rather than dereferencing a null pointer, so a consumer can
    // call straight through without inspecting the table first.
    io::Result read(void* buffer, std::size_t length);
    io::Result write(const void* buffer, std::size_t length);
    int open();
    int close();
    int control(unsigned long request, void* argument);

    // 區塊層存取。裝置不是區塊裝置時回傳 Unsupported，大小與數量則回傳 0，
    // 因此呼叫端可以用 block_size() 是否為 0 判斷。
    //
    // Block-level access. A device that is not a block device reports
    // Unsupported, and reports zero for both sizes, so a caller can simply
    // test block_size() against zero.
    io::Result block_read(std::uint64_t first, std::size_t count, void* buffer);
    io::Result block_write(std::uint64_t first, std::size_t count, const void* buffer);
    std::size_t block_size();
    std::uint64_t block_count();
    bool is_block_device();
};

// 註冊表容量。固定大小的表沒有配置失敗這種狀態，而目前一台機器上的裝置用
// 手指就數得完；不夠時把這個數字改大就是全部要做的事。
//
// The registry's capacity. A fixed-size table has no allocation failure to
// handle, and today's machines have a countable handful of devices. Raising
// this number is the whole of what growing it takes.
constexpr std::size_t max_devices = 64;

// 註冊表操作的結果。註冊失敗的原因會影響驅動程式該怎麼辦——名字撞了是程式
// 錯誤，表滿了是設定問題——因此不是一個布林值。
//
// The outcome of a registry operation. Why a registration failed changes what
// the driver should do about it — a name collision is a programming error
// while a full table is a configuration one — so this is not a boolean.
enum class Status : std::uint8_t { Ok, InvalidArgument, DuplicateName, RegistryFull, NotRegistered };

// 清空註冊表；必須在任何驅動程式註冊之前呼叫。
// Clear the registry. Must run before any driver registers.
void initialize();

// 把裝置登記到註冊表。名稱為空、過長或沒有操作表時回傳 InvalidArgument；
// 名稱已被佔用時回傳 DuplicateName；表滿時回傳 RegistryFull。
//
// Publish a device in the registry. An empty or over-long name, or a missing
// operation table, returns InvalidArgument; a name already taken returns
// DuplicateName; a full table returns RegistryFull.
Status register_device(Device& device);
// 從註冊表移除裝置。不在表中時回傳 NotRegistered。
// Remove a device from the registry. One that is not present returns
// NotRegistered.
Status unregister_device(Device& device);

// 依名稱查詢；找不到時回傳 nullptr。
// Look a device up by name; returns nullptr when there is none.
Device* find(const char* name);

// 目前已註冊的裝置數，以及依索引取得裝置。索引不保證穩定：移除一個裝置會
// 讓最後一個補上它的位置，因此列舉途中不要註冊或移除裝置。
//
// How many devices are registered, and access by index. An index is not
// stable: removing a device moves the last one into its slot, so do not
// register or remove devices while enumerating.
std::size_t count();
Device* at(std::size_t index);

// 把註冊表印到主控台，每個裝置一行：
//
//     [device] kbd0 type=input
//
// Print the registry to the console, one line per device:
//
//     [device] kbd0 type=input
void dump();

// 以既有的 io::ByteStream 為後端的操作表，driver_data 指向那個串流。核心裡
// 已經有一批寫成 ByteStream 的東西（中斷輸入佇列就是其中之一），它們變成
// 裝置不應該需要把讀寫邏輯再寫一次。
//
// 這是一個物件而不是一個回傳它的函式，因為靜態的裝置物件必須在編譯期就完成
// 初始化（見上面 Device 建構子的說明）。初始式裡出現一次普通的函式呼叫，
// 整個物件就會退回執行期初始化，而那個初始化永遠不會發生。
//
// An operation table backed by an existing io::ByteStream, with driver_data
// pointing at that stream. The kernel already has things written as byte
// streams — the interrupt input queue among them — and turning one into a
// device should not mean writing its read and write a second time.
//
// It is an object rather than a function returning one because a static device
// object has to be initialized at compile time — see the note on Device's
// constructor above. One ordinary function call in the initializer pushes the
// whole object back to run-time initialization, which never happens.
extern const Operations stream_operations;

// 同樣地，以既有的 io::BlockDevice 為後端的操作表，driver_data 指向那個裝置。
// RAM disk 與之後真正的磁碟驅動程式都已經寫成 BlockDevice，它們變成裝置不必
// 再實作一次區塊定址。
//
// 這張表同時提供位元組層的 read/write：它把檔案位置換算成區塊，讀出整塊之後
// 再取出需要的那一段。位元組層的呼叫沒有位置參數，因此那個換算由 VFS 完成，
// 這裡只提供區塊層的四個操作。
//
// The same idea for an existing io::BlockDevice, with driver_data pointing at
// it. The RAM disk — and a real disk driver later — is already written as a
// BlockDevice, and becoming a device should not mean implementing block
// addressing a second time.
//
// This table supplies only the four block operations: a byte-level call
// carries no position, so translating a file offset into blocks is the VFS's
// job rather than this table's.
extern const Operations block_operations;

// 反過來，把裝置包成位元組串流，讓標準輸入輸出與未來的 VFS 節點可以用同一
// 個介面消費裝置。
//
// The other direction: wrap a device as a byte stream so the standard streams
// and a future VFS node consume devices through one interface.
class Stream final : public io::ByteStream {
public:
    explicit Stream(Device* device = nullptr) : device_(device) {}
    void bind(Device* device) { device_ = device; }
    Device* device() const { return device_; }
    // 未繫結時回傳 Unsupported，而不是當成沒有資料：那兩件事的意思不一樣。
    // An unbound stream returns Unsupported rather than pretending to have no
    // data; those two things do not mean the same thing.
    io::Result read(void* buffer, std::size_t length) override;
    io::Result write(const void* buffer, std::size_t length) override;

private:
    Device* device_ = nullptr;
};

// /dev/null：讀出來永遠是檔案結尾，寫進去的一律接受並丟棄。這是未來 /dev
// 底下第一個不對應任何硬體的裝置，也是驗證抽象層本身的最小例子。
//
// /dev/null: reading always reports end of file and writing accepts and
// discards everything. It is the first device under a future /dev that
// corresponds to no hardware, and the smallest possible check on the
// abstraction itself.
bool null_initialize();

} // namespace shirley::device
