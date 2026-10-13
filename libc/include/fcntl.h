#pragma once

/* open() 的存取模式，與核心 process 層對系統呼叫旗標的解讀一致。 */
/* open()'s access modes, matching how the kernel process layer reads the
   syscall flags. */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
/* 不存在就建立；以寫入開啟時清成空的。數值與 Linux 相同。 */
/* Create when missing; empty the file when opened for writing. The values
   match Linux. */
#define O_CREAT 0100
#define O_TRUNC 01000

/* 開啟一個路徑，回傳描述子或負數錯誤碼。 */
/* Open a path, returning a descriptor or a negative error. */
int open(const char* path, int flags);
