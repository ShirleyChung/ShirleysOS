#include <unistd.h>

extern long long shirley_syscall(long long number, ...);

void _exit(int status) {
    shirley_syscall(2, status);
    /* exit 系統呼叫不返回；萬一返回就停在這裡，別掉進未定義的程式碼。 */
    /* The exit syscall does not return; park here if it ever does rather than
       falling into undefined code. */
    for (;;) {
    }
}
