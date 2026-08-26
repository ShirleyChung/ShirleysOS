#include "uefi.hpp"

#include "shirley/boot/elf64.hpp"
#include "shirley/boot_protocol.hpp"
#include "shirley/platform/firmware/uefi.hpp"

// ShirleyOS 的 UEFI 開機載入器。它在韌體環境中執行，把核心 ELF 讀進實體記憶體，
// 收集記憶體地圖與 framebuffer 資訊，呼叫 ExitBootServices 取得機器控制權，
// 最後帶著 BootHandoff 跳進核心。
//
// The ShirleyOS UEFI boot loader. It runs inside the firmware environment,
// reads the kernel ELF into physical memory, collects the memory map and
// framebuffer description, calls ExitBootServices to take control of the
// machine, and finally jumps into the kernel with a BootHandoff.
namespace shirley::boot::uefi {
namespace {

// UEFI 的記憶體地圖轉換與核心共用同一份實作，位於平台的韌體格式層。
// The UEFI memory map conversion is the same implementation the kernel uses,
// in the platform firmware format layer.
namespace firmware = shirley::platform::firmware;

#if defined(__x86_64__)
constexpr std::uint16_t kernel_machine = elf_machine_x86_64;
const Char16* const kernel_path = u"\\shirley\\kernel.elf";
#elif defined(__aarch64__)
constexpr std::uint16_t kernel_machine = elf_machine_aarch64;
const Char16* const kernel_path = u"\\shirley\\kernel.elf";
#else
#error "Unsupported UEFI architecture"
#endif

// 核心最多可以有這麼多個可載入節區與記憶體區段。UEFI 的地圖通常在 100 筆以內，
// 這裡留出寬裕的空間，因為 ExitBootServices 之後就不能再配置記憶體。
//
// The most loadable segments and memory regions the loader will handle. A UEFI
// map is usually well under 100 entries; the headroom matters because no
// allocation is possible after ExitBootServices.
constexpr std::size_t max_segments = 16;
constexpr std::uint64_t max_regions = 256;

// 載入器以 UEFI 的呼叫慣例編譯，核心卻是 System V ABI。x86_64 上這兩者傳遞
// 第一個參數的暫存器不同（RCX 對 RDI），因此核心進入點必須明確標註慣例。
// AArch64 兩者都是 AAPCS64，不需要特別處理。
//
// The loader is compiled with the UEFI calling convention while the kernel
// uses the System V ABI. On x86_64 the two disagree about the first argument
// register (RCX versus RDI), so the kernel entry point must state its
// convention explicitly. On AArch64 both are AAPCS64 and nothing is needed.
#if defined(__x86_64__)
typedef void(__attribute__((sysv_abi)) * KernelEntry)(const BootHandoff*);
#else
typedef void (*KernelEntry)(const BootHandoff*);
#endif

SystemTable* system_table = nullptr;
BootServices* boot_services = nullptr;

// Firmware ConsoleOut is often graphical even when QEMU itself is running with
// -nographic.  Keep a tiny, allocation-free UART path in the loader so every
// stage before the kernel console is visible in the same captured log.
#if defined(__x86_64__)
constexpr std::uint16_t serial_base = 0x3f8;

void serial_out(std::uint16_t port, std::uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

std::uint8_t serial_in(std::uint16_t port) {
    std::uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_initialize() {
    serial_out(serial_base + 1, 0x00);
    serial_out(serial_base + 3, 0x80);
    serial_out(serial_base, 0x03);
    serial_out(serial_base + 1, 0x00);
    serial_out(serial_base + 3, 0x03);
    serial_out(serial_base + 2, 0xc7);
    serial_out(serial_base + 4, 0x03);
}

void serial_put(char value) {
    while ((serial_in(serial_base + 5) & (1u << 5)) == 0) {}
    serial_out(serial_base, static_cast<std::uint8_t>(value));
}
#else
// QEMU virt exposes its PL011 UART at this architectural platform address.
constexpr std::uintptr_t serial_base = 0x09000000;

void serial_initialize() {}

void serial_put(char value) {
    auto* const data = reinterpret_cast<volatile std::uint32_t*>(serial_base);
    auto* const flags = reinterpret_cast<volatile std::uint32_t*>(serial_base + 0x18);
    while ((*flags & (1u << 5)) != 0) {}
    *data = static_cast<std::uint8_t>(value);
}
#endif

void serial_print(const char* text) {
    if (text == nullptr) return;
    for (; *text != '\0'; ++text) {
        if (*text == '\n') serial_put('\r');
        serial_put(*text);
    }
}

// 交接資料必須在 ExitBootServices 之前就備妥，之後只能填值不能配置。
// The handoff data has to exist before ExitBootServices; afterwards it can
// only be filled in, never allocated.
struct Handoff {
    BootHandoff handoff;
    BootMemoryRegion regions[max_regions];
};

void print(const Char16* text) {
    if (system_table != nullptr && system_table->console_out != nullptr)
        system_table->console_out->output_string(system_table->console_out, text);
}

// 只在錯誤路徑上使用，因此不需要完整的格式化功能。
// Only used on error paths, so no full formatting is needed.
void print_hex(std::uint64_t value) {
    Char16 buffer[19];
    buffer[0] = u'0';
    buffer[1] = u'x';
    for (unsigned i = 0; i < 16; ++i)
        buffer[2 + i] = u"0123456789abcdef"[(value >> ((15 - i) * 4)) & 0xf];
    buffer[18] = u'\0';
    print(buffer);
}

// 開機失敗時停在韌體環境，讓錯誤訊息留在畫面上。
// On failure, stop inside the firmware environment so the message stays on
// screen.
[[noreturn]] void fail(const Char16* message, Status status) {
    serial_print("[uefi] fatal error\n");
    print(u"\r\nShirleyOS boot failed: ");
    print(message);
    print(u" status=");
    print_hex(status);
    print(u"\r\n");
    for (;;) {
        if (boot_services != nullptr) boot_services->stall(1000000);
    }
}

void fill(void* destination, unsigned char value, std::uint64_t count) {
    auto* bytes = static_cast<unsigned char*>(destination);
    for (std::uint64_t i = 0; i < count; ++i) bytes[i] = value;
}

void copy(void* destination, const void* source, std::uint64_t count) {
    auto* to = static_cast<unsigned char*>(destination);
    const auto* from = static_cast<const unsigned char*>(source);
    for (std::uint64_t i = 0; i < count; ++i) to[i] = from[i];
}

std::uint64_t pages_for(std::uint64_t bytes) {
    return (bytes + firmware::efi_page_size - 1) / firmware::efi_page_size;
}

// 開啟載入器自身所在磁碟區的根目錄。
// Open the root directory of the volume the loader itself came from.
File* open_volume_root(Handle image) {
    LoadedImage* loaded = nullptr;
    auto status = boot_services->handle_protocol(image, &loaded_image_protocol_guid,
                                                 reinterpret_cast<void**>(&loaded));
    if (failed(status)) fail(u"cannot open the loaded image protocol", status);

    SimpleFileSystem* file_system = nullptr;
    status = boot_services->handle_protocol(loaded->device_handle, &simple_file_system_protocol_guid,
                                            reinterpret_cast<void**>(&file_system));
    if (failed(status)) fail(u"the boot volume has no file system", status);

    File* root = nullptr;
    status = file_system->open_volume(file_system, &root);
    if (failed(status)) fail(u"cannot open the boot volume", status);
    return root;
}

// 讀入整個核心檔案。回傳緩衝區位址，並在 size 帶回實際大小。
// Read the whole kernel file. Returns the buffer and reports the real size.
void* read_kernel(File* root, std::uint64_t& size) {
    File* file = nullptr;
    auto status = root->open(root, &file, kernel_path, file_mode_read, 0);
    if (failed(status)) fail(u"cannot open \\shirley\\kernel.elf", status);

    // FileInfo 的長度會隨檔名而變，先問韌體需要多少空間。
    // The length of FileInfo depends on the file name, so ask the firmware how
    // much space it needs first.
    std::uintptr_t info_size = 0;
    status = file->get_info(file, &file_info_guid, &info_size, nullptr);
    if (status != buffer_too_small) fail(u"cannot size the kernel file information", status);

    void* info_buffer = nullptr;
    status = boot_services->allocate_pool(loader_data, info_size, &info_buffer);
    if (failed(status)) fail(u"cannot allocate the kernel file information", status);
    status = file->get_info(file, &file_info_guid, &info_size, info_buffer);
    if (failed(status)) fail(u"cannot read the kernel file information", status);

    const auto file_size = static_cast<const FileInfo*>(info_buffer)->file_size;
    boot_services->free_pool(info_buffer);
    if (file_size == 0) fail(u"the kernel file is empty", success);

    // 以分頁配置核心映像，之後才能沿用同一塊記憶體走訪 ELF 標頭。
    // Allocate the image in pages so the same memory can be walked as an ELF.
    std::uint64_t buffer = 0;
    status = boot_services->allocate_pages(allocate_any_pages, loader_data,
                                           static_cast<std::uintptr_t>(pages_for(file_size)), &buffer);
    if (failed(status)) fail(u"cannot allocate memory for the kernel file", status);

    auto remaining = static_cast<std::uintptr_t>(file_size);
    status = file->read(file, &remaining, reinterpret_cast<void*>(buffer));
    if (failed(status)) fail(u"cannot read the kernel file", status);
    if (remaining != file_size) fail(u"the kernel file was read only partially", success);
    file->close(file);

    size = file_size;
    return reinterpret_cast<void*>(buffer);
}

// 依 program header 把核心搬到它要求的實體位址。
// Place the kernel at the physical addresses its program headers ask for.
std::uint64_t load_kernel(const void* image, std::uint64_t size, std::uint64_t& image_start,
                          std::uint64_t& image_end) {
    if (!elf64_valid(image, static_cast<std::size_t>(size), kernel_machine))
        fail(u"the kernel is not a loadable ELF64 executable for this architecture", success);

    Elf64Segment segments[max_segments];
    const auto count =
        elf64_segments(image, static_cast<std::size_t>(size), kernel_machine, segments, max_segments);
    if (count == 0) fail(u"the kernel has no loadable segments", success);
    if (!elf64_physical_extent(segments, count, image_start, image_end))
        fail(u"the kernel segments cover no memory", success);

    // 一次要下整個映像範圍，個別節區分開要會在跨頁時互相衝突。
    // Claim the whole image range in one request; per-segment requests collide
    // whenever two segments share a page.
    const auto base = image_start & ~(firmware::efi_page_size - 1);
    const auto pages = pages_for(image_end - base);
    std::uint64_t allocated = base;
    const auto status = boot_services->allocate_pages(allocate_address, loader_data,
                                                      static_cast<std::uintptr_t>(pages), &allocated);
    if (failed(status)) {
        print(u"\r\nThe kernel wants physical address ");
        print_hex(base);
        fail(u" but the firmware will not reserve it", status);
    }

    const auto* bytes = static_cast<const unsigned char*>(image);
    for (std::size_t i = 0; i < count; ++i) {
        auto* target = reinterpret_cast<void*>(segments[i].physical_address);
        copy(target, bytes + segments[i].file_offset, segments[i].file_size);
        // memory_size 超出 file_size 的部分是 .bss。
        // Whatever memory_size has beyond file_size is .bss.
        fill(static_cast<unsigned char*>(target) + segments[i].file_size, 0,
             segments[i].memory_size - segments[i].file_size);
    }
    image_start = base;
    image_end = base + pages * firmware::efi_page_size;
    return elf64_entry(image, static_cast<std::size_t>(size), kernel_machine);
}

// GOP 若存在就記錄 framebuffer；沒有顯示輸出並不是錯誤。
// Record the framebuffer when GOP is present. Having no display is not an
// error.
void read_framebuffer(FramebufferInfo& framebuffer) {
    GraphicsOutput* graphics = nullptr;
    const auto status = boot_services->locate_protocol(&graphics_output_protocol_guid, nullptr,
                                                       reinterpret_cast<void**>(&graphics));
    if (failed(status) || graphics == nullptr || graphics->mode == nullptr) return;
    const auto* mode = graphics->mode;
    const auto* information = mode->information;
    if (information == nullptr || mode->frame_buffer_base == 0) return;
    if (information->pixel_format == pixel_blt_only) return;

    framebuffer.address = mode->frame_buffer_base;
    framebuffer.width = information->horizontal_resolution;
    framebuffer.height = information->vertical_resolution;
    // UEFI 以像素為單位回報每列長度，BootInfo 的 pitch 是位元組。
    // UEFI reports the scanline in pixels; BootInfo's pitch is in bytes.
    framebuffer.pitch = information->pixels_per_scan_line * 4;
    framebuffer.format = information->pixel_format;
}

// 在系統設定表中找出 ACPI 2.0 的 RSDP。
// Find the ACPI 2.0 RSDP in the configuration table.
void* find_acpi_table() {
    for (std::uintptr_t i = 0; i < system_table->configuration_table_count; ++i) {
        const auto& entry = system_table->configuration_table[i];
        if (guid_equal(entry.vendor_guid, acpi_table_guid)) return entry.vendor_table;
    }
    return nullptr;
}

// 取得記憶體地圖並離開 boot services。map_key 必須來自最後一次 GetMemoryMap，
// 中間只要再配置一次記憶體就會失效，因此失敗時整個流程重來。
//
// Get the memory map and leave boot services. The map_key must come from the
// most recent GetMemoryMap, and any allocation in between invalidates it, so a
// failure retries the whole sequence.
std::uint64_t exit_boot_services(Handle image, void* map, std::uintptr_t map_capacity,
                                 std::uintptr_t& descriptor_size, BootMemoryRegion* regions,
                                 std::uint64_t capacity) {
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        std::uintptr_t map_size = map_capacity;
        std::uintptr_t map_key = 0;
        std::uint32_t descriptor_version = 0;
        auto status = boot_services->get_memory_map(&map_size, map, &map_key, &descriptor_size,
                                                    &descriptor_version);
        if (failed(status)) fail(u"cannot read the UEFI memory map", status);
        if (descriptor_size < sizeof(firmware::EfiMemoryDescriptor) ||
            map_size / descriptor_size >= capacity)
            fail(u"the UEFI memory map has too many entries", buffer_too_small);

        status = boot_services->exit_boot_services(image, map_key);
        if (failed(status)) {
            // 只有 map_key 過期是可以重試的；其他錯誤代表機器狀態有問題。
            // Only a stale map_key is worth retrying; anything else means the
            // machine is in a state this loader cannot recover from.
            if (status != invalid_parameter) fail(u"ExitBootServices refused the handoff", status);
            continue;
        }
        // 從這裡開始不能再呼叫任何 boot service。
        // From here on no boot service may be called.
        boot_services = nullptr;
        system_table = nullptr;
        return firmware::uefi_memory_map(map, map_size, descriptor_size, regions, capacity);
    }
    fail(u"the UEFI memory map kept changing", success);
}

} // namespace
} // namespace shirley::boot::uefi

extern "C" shirley::boot::uefi::Status efi_main(shirley::boot::uefi::Handle image,
                                                shirley::boot::uefi::SystemTable* system) {
    using namespace shirley;
    using namespace shirley::boot;
    using namespace shirley::boot::uefi;

    system_table = system;
    boot_services = system->boot_services;
    serial_initialize();
    serial_print("[uefi] entered EFI application\n");
    print(u"ShirleyOS UEFI boot loader\r\n");

    auto* root = open_volume_root(image);
    std::uint64_t file_size = 0;
    auto* file_image = read_kernel(root, file_size);
    root->close(root);
    serial_print("[uefi] kernel ELF read\n");

    std::uint64_t kernel_start = 0;
    std::uint64_t kernel_end = 0;
    const auto entry = load_kernel(file_image, file_size, kernel_start, kernel_end);
    if (entry == 0) fail(u"the kernel has no entry point", success);
    serial_print("[uefi] kernel ELF loaded\n");

    // 交接結構與記憶體地圖緩衝區都要在 ExitBootServices 之前配置好。
    // Both the handoff structure and the memory map buffer must be allocated
    // before ExitBootServices.
    std::uint64_t handoff_pages = 0;
    auto status = boot_services->allocate_pages(allocate_any_pages, loader_data,
                                               static_cast<std::uintptr_t>(pages_for(sizeof(Handoff))),
                                               &handoff_pages);
    if (failed(status)) fail(u"cannot allocate the boot handoff", status);
    auto* handoff = reinterpret_cast<Handoff*>(handoff_pages);
    fill(handoff, 0, sizeof(Handoff));
    handoff->handoff.magic = BootHandoff::magic_value;
    handoff->handoff.version = BootHandoff::current_version;
    handoff->handoff.size = sizeof(BootHandoff);
    handoff->handoff.info.version = BootInfo::current_version;

    read_framebuffer(handoff->handoff.info.framebuffer);
    handoff->handoff.info.firmware_data = find_acpi_table();

    // 記憶體地圖會因為接下來的配置而變大，因此多留幾頁的餘裕。
    // The map grows because of the allocations that follow, so reserve a few
    // spare pages.
    std::uintptr_t map_size = 0;
    std::uintptr_t map_key = 0;
    std::uintptr_t descriptor_size = 0;
    std::uint32_t descriptor_version = 0;
    status = boot_services->get_memory_map(&map_size, nullptr, &map_key, &descriptor_size,
                                           &descriptor_version);
    if (status != buffer_too_small || map_size == 0 ||
        descriptor_size < sizeof(firmware::EfiMemoryDescriptor))
        fail(u"cannot size the UEFI memory map", status);
    const auto map_capacity = map_size + 8 * firmware::efi_page_size;
    std::uint64_t map_pages = 0;
    status = boot_services->allocate_pages(allocate_any_pages, loader_data,
                                           static_cast<std::uintptr_t>(pages_for(map_capacity)),
                                           &map_pages);
    if (failed(status)) fail(u"cannot allocate the UEFI memory map", status);

    print(u"Handing control to the ShirleyOS kernel\r\n");
    auto count = exit_boot_services(image, reinterpret_cast<void*>(map_pages),
                                    static_cast<std::uintptr_t>(map_capacity), descriptor_size,
                                    handoff->regions, max_regions);
    serial_print("[uefi] boot services exited\n");

    // 核心映像與交接資料本身都還在使用中，必須排除在可用記憶體之外。
    // The kernel image and the handoff data are both still live and must be
    // kept out of usable memory.
    if (count + 2 <= max_regions) {
        handoff->regions[count++] = {kernel_start, kernel_end - kernel_start, MemoryType::Reserved};
        handoff->regions[count++] = {handoff_pages,
                                     pages_for(sizeof(Handoff)) * firmware::efi_page_size,
                                     MemoryType::Reserved};
    }
    handoff->handoff.info.memory_regions = handoff->regions;
    handoff->handoff.info.memory_region_count = count;

    // 跳進核心；核心的進入點永遠不會返回。
    // Jump into the kernel. Its entry point never returns.
    auto kernel = reinterpret_cast<KernelEntry>(entry);
    serial_print("[uefi] entering kernel\n");
    kernel(&handoff->handoff);
    for (;;) {}
}
