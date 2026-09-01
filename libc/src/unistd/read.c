#include <unistd.h>

extern long long shirley_syscall(long long number, ...);

ssize_t read(int descriptor, void* buffer, size_t length) {
    return (ssize_t)shirley_syscall(3, descriptor, buffer, length);
}
