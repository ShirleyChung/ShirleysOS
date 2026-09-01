#include <unistd.h>

extern long long shirley_syscall(long long number, ...);

int close(int descriptor) {
    return (int)shirley_syscall(5, descriptor);
}
