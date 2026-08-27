#include "shirley/user_loader.hpp"

extern "C" {
extern const unsigned char _binary_user_hello_hello_elf_start[];
extern const unsigned char _binary_user_hello_hello_elf_end[];
}

namespace shirley::user {

const void* embedded_image() { return _binary_user_hello_hello_elf_start; }
std::size_t embedded_image_size() {
    return static_cast<std::size_t>(_binary_user_hello_hello_elf_end -
                                    _binary_user_hello_hello_elf_start);
}

} // namespace shirley::user
