#include <unistd.h>

extern long long shirley_syscall(long long number, ...);

ssize_t write(int descriptor, const void* buffer, size_t length) {
    return (ssize_t)shirley_syscall(1, descriptor, buffer, length);
}
