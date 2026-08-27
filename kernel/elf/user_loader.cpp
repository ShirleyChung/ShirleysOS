#include "shirley/user_loader.hpp"

#include "shirley/memory.hpp"

namespace shirley::user {
namespace {

constexpr std::size_t max_segments = 16;
constexpr std::uint64_t user_stack_top = 0x00800000;
constexpr std::uint64_t user_stack_page = user_stack_top - memory::page_size;

std::uint64_t align_down(std::uint64_t value) {
    return value & ~(static_cast<std::uint64_t>(memory::page_size) - 1);
}

bool add_overflow(std::uint64_t left, std::uint64_t right, std::uint64_t& result) {
    if (left > (~static_cast<std::uint64_t>(0)) - right) return true;
    result = left + right;
    return false;
}

memory::PageFlags flags_for(const boot::Elf64Segment& segment) {
    memory::PageFlags flags = memory::PageFlags::User;
    if (segment.readable) flags |= memory::PageFlags::Read;
    if (segment.writable) flags |= memory::PageFlags::Write;
    if (segment.executable) flags |= memory::PageFlags::Execute;
    return flags;
}

void zero_page(memory::PhysicalAddress page) {
    auto* bytes = reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(page));
    for (std::size_t i = 0; i < memory::page_size; ++i) bytes[i] = 0;
}

void copy_bytes(memory::PhysicalAddress page, std::size_t offset,
                const unsigned char* source, std::size_t count) {
    auto* destination = reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(page)) + offset;
    for (std::size_t i = 0; i < count; ++i) destination[i] = source[i];
}

} // namespace

bool load_elf(const void* image, std::size_t size, std::uint16_t machine,
              memory::AddressSpace& address_space, Image& result) {
    boot::Elf64Segment segments[max_segments]{};
    const auto count = boot::elf64_segments(image, size, machine, segments, max_segments);
    if (count == 0 || count == max_segments) return false;

    const auto entry = boot::elf64_entry(image, size, machine);
    if (entry == 0 || entry >= user_stack_page) return false;

    const auto* bytes = static_cast<const unsigned char*>(image);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& segment = segments[i];
        if (segment.memory_size == 0 || segment.file_size > segment.memory_size) return false;
        std::uint64_t segment_end = 0;
        if (add_overflow(segment.virtual_address, segment.memory_size, segment_end) ||
            segment_end > user_stack_page || segment.virtual_address < memory::page_size)
            return false;
        const auto first = align_down(segment.virtual_address);
        const auto last = align_down(segment_end - 1) + memory::page_size;
        for (auto address = first; address < last; address += memory::page_size) {
            const auto page = memory::allocate_page();
            if (page == 0 || !address_space.map(address, page, flags_for(segment))) return false;
            zero_page(page);
        }
        // PT_LOAD file ranges were already bounds-checked by elf64_segments().
        std::uint64_t file_end = 0;
        if (add_overflow(segment.virtual_address, segment.file_size, file_end)) return false;
        for (std::uint64_t address = segment.virtual_address; address < file_end;) {
            const auto page_address = align_down(address);
            const auto page = address_space.translate(address);
            if (page == 0) return false;
            const auto offset = static_cast<std::size_t>(address - page_address);
            const auto remaining_page = memory::page_size - offset;
            const auto remaining_file = file_end - address;
            const auto amount = remaining_file < remaining_page ? remaining_file : remaining_page;
            copy_bytes(page, offset, bytes + segment.file_offset + (address - segment.virtual_address),
                       static_cast<std::size_t>(amount));
            address += amount;
        }
    }

    const auto stack_page = memory::allocate_page();
    if (stack_page == 0 || !address_space.map(user_stack_page, stack_page,
                                               memory::PageFlags::Read | memory::PageFlags::Write |
                                                   memory::PageFlags::User))
        return false;
    zero_page(stack_page);
    result = {entry, user_stack_top, user_stack_page, count};
    return true;
}

} // namespace shirley::user
