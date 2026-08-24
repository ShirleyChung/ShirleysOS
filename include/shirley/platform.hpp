#pragma once

#include "shirley/boot_info.hpp"

namespace shirley::platform {

// 初始化平台層，並取得平台名稱。
void initialize(const BootInfo&);
const char* name();

} // namespace shirley::platform
