#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    // 使用者程式的最小示範：透過 libc 輸出問候訊息。
    // The smallest possible user program: greet through libc.
    printf("Hello! Shirley's OS.\n");

    // 接著用 open/read/write/close 這組系統呼叫從檔案系統讀出一個檔案。這證明
    // 使用者程式現在能自己走 VFS，而不只是把位元組寫到標準輸出；讀完就結束，
    // 控制權會回到 shell。
    //
    // Then read a file from the file system through the open/read/write/close
    // syscalls. This proves a user program can now reach the VFS itself rather
    // than only writing bytes to standard output; it exits when done and
    // control returns to the shell.
    int descriptor = open("/etc/version", O_RDONLY);
    if (descriptor >= 0) {
        printf("/etc/version: ");
        char buffer[128];
        ssize_t count;
        while ((count = read(descriptor, buffer, sizeof(buffer))) > 0) {
            write(1, buffer, (size_t)count);
        }
        close(descriptor);
    }
    return 0;
}
