#pragma once

#include "shirley/boot_info.hpp"

namespace shirley::platform {

void initialize(const BootInfo&);
const char* name();

} // namespace shirley::platform
