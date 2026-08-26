#include "shirley/boot/elf64.hpp"

namespace shirley::boot {
namespace {

// ELF64 檔案標頭中我們需要的欄位。
// The fields of the ELF64 file header this reader needs.
struct Header {
    unsigned char identifier[16];
    std::uint16_t type;
    std::uint16_t machine;
    std::uint32_t version;
    std::uint64_t entry;
    std::uint64_t program_header_offset;
    std::uint64_t section_header_offset;
    std::uint32_t flags;
    std::uint16_t header_size;
    std::uint16_t program_header_entry_size;
    std::uint16_t program_header_count;
    std::uint16_t section_header_entry_size;
    std::uint16_t section_header_count;
    std::uint16_t section_name_index;
};

struct ProgramHeader {
    std::uint32_t type;
    std::uint32_t flags;
    std::uint64_t offset;
    std::uint64_t virtual_address;
    std::uint64_t physical_address;
    std::uint64_t file_size;
    std::uint64_t memory_size;
    std::uint64_t alignment;
};

constexpr std::uint32_t program_type_load = 1;
constexpr std::uint32_t flag_executable = 1u << 0;
constexpr std::uint32_t flag_writable = 1u << 1;
constexpr std::uint32_t flag_readable = 1u << 2;
// e_type 為 2 代表 ET_EXEC，也就是位址固定的執行檔。
// e_type 2 is ET_EXEC, an executable with fixed addresses.
constexpr std::uint16_t type_executable = 2;

// identifier 的前七個位元組：magic、64 位元、小端序、目前的 ELF 版本。
// The first seven identifier bytes: magic, 64-bit, little-endian, current ELF
// version.
bool identifier_ok(const unsigned char* identifier) {
    return identifier[0] == 0x7f && identifier[1] == 'E' && identifier[2] == 'L' &&
           identifier[3] == 'F' && identifier[4] == 2 && identifier[5] == 1 && identifier[6] == 1;
}

// 讀取 buffer 時一律先確認範圍，因為 ELF 內容來自磁碟而非核心自己產生。
// Every read is range-checked first, because the ELF comes from disk rather
// than from the kernel itself.
bool range_ok(std::uint64_t offset, std::uint64_t length, std::size_t size) {
    if (offset > size) return false;
    return length <= size - offset;
}

const Header* header_of(const void* buffer, std::size_t size, std::uint16_t machine) {
    if (buffer == nullptr || size < sizeof(Header)) return nullptr;
    const auto* header = static_cast<const Header*>(buffer);
    if (!identifier_ok(header->identifier)) return nullptr;
    if (header->type != type_executable || header->machine != machine) return nullptr;
    if (header->program_header_entry_size < sizeof(ProgramHeader)) return nullptr;
    if (header->program_header_count == 0) return nullptr;
    const std::uint64_t table_bytes =
        static_cast<std::uint64_t>(header->program_header_entry_size) * header->program_header_count;
    if (!range_ok(header->program_header_offset, table_bytes, size)) return nullptr;
    return header;
}

const ProgramHeader* program_header_at(const Header* header, const void* buffer, std::size_t index) {
    // 以宣告的 entry size 為步幅，不能假設它等於 sizeof(ProgramHeader)。
    // Stride by the declared entry size; it cannot be assumed to equal
    // sizeof(ProgramHeader).
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    return reinterpret_cast<const ProgramHeader*>(
        bytes + header->program_header_offset + index * header->program_header_entry_size);
}

} // namespace

bool elf64_valid(const void* buffer, std::size_t size, std::uint16_t machine) {
    const auto* header = header_of(buffer, size, machine);
    if (header == nullptr) return false;
    // 任何一個 PT_LOAD 的檔案範圍越界就整份拒絕。
    // A single PT_LOAD whose file range is out of bounds rejects the whole ELF.
    for (std::size_t i = 0; i < header->program_header_count; ++i) {
        const auto* program = program_header_at(header, buffer, i);
        if (program->type != program_type_load) continue;
        if (!range_ok(program->offset, program->file_size, size)) return false;
        if (program->memory_size < program->file_size) return false;
    }
    return true;
}

std::uint64_t elf64_entry(const void* buffer, std::size_t size, std::uint16_t machine) {
    if (!elf64_valid(buffer, size, machine)) return 0;
    return static_cast<const Header*>(buffer)->entry;
}

std::size_t elf64_segments(const void* buffer, std::size_t size, std::uint16_t machine,
                           Elf64Segment* segments, std::size_t capacity) {
    if (segments == nullptr || capacity == 0) return 0;
    if (!elf64_valid(buffer, size, machine)) return 0;
    const auto* header = static_cast<const Header*>(buffer);
    std::size_t count = 0;
    for (std::size_t i = 0; i < header->program_header_count && count < capacity; ++i) {
        const auto* program = program_header_at(header, buffer, i);
        if (program->type != program_type_load || program->memory_size == 0) continue;
        segments[count++] = {program->offset,
                             program->file_size,
                             program->physical_address,
                             program->virtual_address,
                             program->memory_size,
                             (program->flags & flag_readable) != 0,
                             (program->flags & flag_writable) != 0,
                             (program->flags & flag_executable) != 0};
    }
    return count;
}

bool elf64_physical_extent(const Elf64Segment* segments, std::size_t count,
                           std::uint64_t& lowest, std::uint64_t& end) {
    if (segments == nullptr || count == 0) return false;
    bool found = false;
    for (std::size_t i = 0; i < count; ++i) {
        const auto start = segments[i].physical_address;
        const auto finish = start + segments[i].memory_size;
        if (!found) {
            lowest = start;
            end = finish;
            found = true;
            continue;
        }
        if (start < lowest) lowest = start;
        if (finish > end) end = finish;
    }
    return found;
}

} // namespace shirley::boot
