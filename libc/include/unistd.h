#pragma once

#include <stddef.h>

typedef long ssize_t;

/* ShirleyOS 的最小 POSIX 相容輸出介面。 */
/* ShirleyOS's minimal POSIX-compatible output interface. */
ssize_t write(int descriptor, const void* buffer, size_t length);
