#pragma once

#include <stddef.h>

typedef long ssize_t;

/* ShirleyOS 的最小 POSIX 相容輸入輸出介面。 */
/* ShirleyOS's minimal POSIX-compatible input/output interface. */
ssize_t write(int descriptor, const void* buffer, size_t length);
ssize_t read(int descriptor, void* buffer, size_t length);
int close(int descriptor);
/* 以結束碼終止行程；不返回。 */
/* Terminate the process with a status; does not return. */
void _exit(int status);
