#pragma once

#include <cstdint>

namespace shirley::arch {

using AddressSpaceHandle = std::uintptr_t;

void initialize();
void enable_interrupts();
void disable_interrupts();
[[noreturn]] void halt();
std::uintptr_t current_stack_pointer();
void switch_address_space(AddressSpaceHandle);
[[noreturn]] void enter_userspace(std::uintptr_t entry, std::uintptr_t user_stack);

} // namespace shirley::arch
