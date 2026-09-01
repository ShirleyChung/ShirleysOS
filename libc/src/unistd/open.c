#include <fcntl.h>

extern long long shirley_syscall(long long number, ...);

int open(const char* path, int flags) {
    return (int)shirley_syscall(4, path, flags);
}
