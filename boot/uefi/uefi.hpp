#pragma once

#include <cstddef>
#include <cstdint>

// UEFI 規格中本載入器需要的介面定義。這裡不引用 EDK2 原始碼，只自行宣告
// 用得到的部分，因此不會為了開機而把整套韌體專案拉進建置流程。
//
// 這些結構的欄位順序就是 ABI：函式指標的位移量由 UEFI 規格固定，即使某個函式
// 我們永遠不會呼叫，也必須保留它的位置，否則後面的函式全部會錯位。
//
// The parts of the UEFI specification this loader needs. EDK2 sources are not
// used; only the pieces actually called are declared here, so booting does not
// drag an entire firmware project into the build.
//
// The field order of these structures is the ABI: the UEFI specification fixes
// the offset of every function pointer, so even a function this loader never
// calls must keep its slot, or everything after it shifts.
namespace shirley::boot::uefi {

using Status = std::uintptr_t;
using Handle = void*;
using Event = void*;
using Char16 = char16_t;

// EFI_STATUS 的最高位元表示錯誤。
// The top bit of EFI_STATUS marks an error.
constexpr Status success = 0;
constexpr Status status_error_bit = static_cast<Status>(1) << (sizeof(Status) * 8 - 1);
constexpr Status buffer_too_small = status_error_bit | 5;
constexpr Status invalid_parameter = status_error_bit | 2;
inline bool failed(Status status) { return (status & status_error_bit) != 0; }

struct Guid {
    std::uint32_t data1;
    std::uint16_t data2;
    std::uint16_t data3;
    std::uint8_t data4[8];
};

// UEFI 使用 Microsoft x64 呼叫慣例；在 AArch64 上則是標準 AAPCS64。
// 以 windows 目標編譯即可取得正確的慣例，因此不需要額外的屬性。
//
// UEFI uses the Microsoft x64 calling convention, and plain AAPCS64 on
// AArch64. Compiling for a windows target gives the right convention, so no
// extra attribute is needed.
struct SimpleTextOutput {
    void* reset;
    Status(*output_string)(SimpleTextOutput*, const Char16*);
    void* test_string;
    void* query_mode;
    void* set_mode;
    Status(*set_attribute)(SimpleTextOutput*, std::uintptr_t);
    Status(*clear_screen)(SimpleTextOutput*);
    void* set_cursor_position;
    void* enable_cursor;
    void* mode;
};

struct TableHeader {
    std::uint64_t signature;
    std::uint32_t revision;
    std::uint32_t header_size;
    std::uint32_t crc32;
    std::uint32_t reserved;
};

// EFI_ALLOCATE_TYPE。
// EFI_ALLOCATE_TYPE.
enum AllocateType : std::uint32_t {
    allocate_any_pages = 0,
    allocate_max_address = 1,
    allocate_address = 2,
};

// EFI_MEMORY_TYPE 中載入器會要求的幾種。
// The EFI_MEMORY_TYPE values this loader asks for.
enum EfiMemoryType : std::uint32_t {
    loader_code = 1,
    loader_data = 2,
};

struct BootServices {
    TableHeader header;
    // 任務優先權服務 / Task priority services.
    void* raise_tpl;
    void* restore_tpl;
    // 記憶體服務 / Memory services.
    Status(*allocate_pages)(AllocateType, EfiMemoryType, std::uintptr_t pages, std::uint64_t* memory);
    Status(*free_pages)(std::uint64_t memory, std::uintptr_t pages);
    Status(*get_memory_map)(std::uintptr_t* map_size, void* map, std::uintptr_t* map_key,
                            std::uintptr_t* descriptor_size, std::uint32_t* descriptor_version);
    Status(*allocate_pool)(EfiMemoryType, std::uintptr_t size, void** buffer);
    Status(*free_pool)(void* buffer);
    // 事件與計時器服務 / Event and timer services.
    void* create_event;
    void* set_timer;
    void* wait_for_event;
    void* signal_event;
    void* close_event;
    void* check_event;
    // 協定處理服務 / Protocol handler services.
    void* install_protocol_interface;
    void* reinstall_protocol_interface;
    void* uninstall_protocol_interface;
    Status(*handle_protocol)(Handle, const Guid*, void** interface_pointer);
    void* reserved;
    void* register_protocol_notify;
    void* locate_handle;
    void* locate_device_path;
    void* install_configuration_table;
    // 映像服務 / Image services.
    void* load_image;
    void* start_image;
    void* exit_image;
    void* unload_image;
    Status(*exit_boot_services)(Handle image, std::uintptr_t map_key);
    // 其他服務 / Miscellaneous services.
    void* get_next_monotonic_count;
    Status(*stall)(std::uintptr_t microseconds);
    void* set_watchdog_timer;
    // 驅動程式支援服務 / Driver support services.
    void* connect_controller;
    void* disconnect_controller;
    // 協定開關服務 / Open and close protocol services.
    void* open_protocol;
    void* close_protocol;
    void* open_protocol_information;
    // 程式庫服務 / Library services.
    void* protocols_per_handle;
    void* locate_handle_buffer;
    Status(*locate_protocol)(const Guid*, void* registration, void** interface_pointer);
    void* install_multiple_protocol_interfaces;
    void* uninstall_multiple_protocol_interfaces;
    // 32 位元 CRC 服務 / 32-bit CRC services.
    void* calculate_crc32;
    // 其他服務 / Miscellaneous services.
    void* copy_mem;
    void* set_mem;
    void* create_event_ex;
};

struct ConfigurationTable {
    Guid vendor_guid;
    void* vendor_table;
};

struct SystemTable {
    TableHeader header;
    Char16* firmware_vendor;
    std::uint32_t firmware_revision;
    Handle console_in_handle;
    void* console_in;
    Handle console_out_handle;
    SimpleTextOutput* console_out;
    Handle standard_error_handle;
    SimpleTextOutput* standard_error;
    void* runtime_services;
    BootServices* boot_services;
    std::uintptr_t configuration_table_count;
    ConfigurationTable* configuration_table;
};

// EFI_LOADED_IMAGE_PROTOCOL，用來找出載入器自己是從哪個裝置啟動的。
// EFI_LOADED_IMAGE_PROTOCOL, used to find which device the loader itself came
// from.
constexpr Guid loaded_image_protocol_guid = {
    0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

struct LoadedImage {
    std::uint32_t revision;
    Handle parent_handle;
    SystemTable* system_table;
    Handle device_handle;
    void* file_path;
    void* reserved;
    std::uint32_t load_options_size;
    void* load_options;
    void* image_base;
    std::uint64_t image_size;
    EfiMemoryType image_code_type;
    EfiMemoryType image_data_type;
    void* unload;
};

// EFI_SIMPLE_FILE_SYSTEM_PROTOCOL 與 EFI_FILE_PROTOCOL。
// EFI_SIMPLE_FILE_SYSTEM_PROTOCOL and EFI_FILE_PROTOCOL.
constexpr Guid simple_file_system_protocol_guid = {
    0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

constexpr std::uint64_t file_mode_read = 0x0000000000000001ull;

struct File {
    std::uint64_t revision;
    Status(*open)(File*, File** result, const Char16* name, std::uint64_t mode,
                  std::uint64_t attributes);
    Status(*close)(File*);
    void* del;
    Status(*read)(File*, std::uintptr_t* size, void* buffer);
    void* write;
    Status(*get_position)(File*, std::uint64_t* position);
    Status(*set_position)(File*, std::uint64_t position);
    Status(*get_info)(File*, const Guid* information_type, std::uintptr_t* size, void* buffer);
    void* set_info;
    void* flush;
};

struct SimpleFileSystem {
    std::uint64_t revision;
    Status(*open_volume)(SimpleFileSystem*, File** root);
};

// EFI_FILE_INFO，用來取得檔案大小。
// EFI_FILE_INFO, used to learn a file's size.
constexpr Guid file_info_guid = {
    0x09576e92, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

struct FileInfo {
    std::uint64_t size;
    std::uint64_t file_size;
    std::uint64_t physical_size;
    std::uint8_t create_time[16];
    std::uint8_t last_access_time[16];
    std::uint8_t modification_time[16];
    std::uint64_t attribute;
    Char16 file_name[1];
};

// EFI_GRAPHICS_OUTPUT_PROTOCOL，用來取得 framebuffer 描述。
// EFI_GRAPHICS_OUTPUT_PROTOCOL, used to describe the framebuffer.
constexpr Guid graphics_output_protocol_guid = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};

// EFI_GRAPHICS_PIXEL_FORMAT。
// EFI_GRAPHICS_PIXEL_FORMAT.
enum PixelFormat : std::uint32_t {
    pixel_red_green_blue_reserved8 = 0,
    pixel_blue_green_red_reserved8 = 1,
    pixel_bit_mask = 2,
    pixel_blt_only = 3,
};

struct GraphicsModeInformation {
    std::uint32_t version;
    std::uint32_t horizontal_resolution;
    std::uint32_t vertical_resolution;
    PixelFormat pixel_format;
    std::uint32_t pixel_information[4];
    std::uint32_t pixels_per_scan_line;
};

struct GraphicsMode {
    std::uint32_t max_mode;
    std::uint32_t mode;
    GraphicsModeInformation* information;
    std::uintptr_t size_of_information;
    std::uint64_t frame_buffer_base;
    std::uintptr_t frame_buffer_size;
};

struct GraphicsOutput {
    void* query_mode;
    void* set_mode;
    void* blt;
    GraphicsMode* mode;
};

// ACPI 2.0 的 RSDP 放在系統設定表中，核心之後需要它來找 ACPI 表格。
// The ACPI 2.0 RSDP lives in the configuration table; the kernel needs it
// later to find the ACPI tables.
constexpr Guid acpi_table_guid = {
    0x8868e871, 0xe4f1, 0x11d3, {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}};

inline bool guid_equal(const Guid& left, const Guid& right) {
    if (left.data1 != right.data1 || left.data2 != right.data2 || left.data3 != right.data3)
        return false;
    for (unsigned i = 0; i < 8; ++i)
        if (left.data4[i] != right.data4[i]) return false;
    return true;
}

} // namespace shirley::boot::uefi
