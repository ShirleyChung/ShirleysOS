#pragma once

#include <cstdint>

namespace shirley::syscall {

// ShirleyOS 原生系統呼叫 ABI。使用者程式把系統呼叫編號放在第一個暫存器、最多
// 三個引數放在其後（x86_64 是 rax/rdi/rsi/rdx，ARM64 是 x8/x0/x1/x2），透過
// x86_64 的 int 0x80 或 ARM64 的 svc #0 進入核心，結果由第一個引數暫存器帶回。
// 這些編號與 libc/arch/<arch>/syscall.S 的 trampoline 以及 libc 包裝函式一致。
//
// ShirleyOS's native syscall ABI. A user program puts the syscall number in the
// first register and up to three arguments after it (rax/rdi/rsi/rdx on
// x86_64, x8/x0/x1/x2 on ARM64), traps in with int 0x80 on x86_64 or svc #0 on
// ARM64, and receives the result in the first argument register. These numbers
// match the trampoline in libc/arch/<arch>/syscall.S and the libc wrappers.
enum class Number : std::uint64_t {
    Write = 1,  // write(fd, buffer, length)
    Exit = 2,   // exit(status)，不返回 / exit(status), does not return
    Read = 3,   // read(fd, buffer, length)
    Open = 4,   // open(path, flags)
    Close = 5,  // close(fd)
};

// Architecture adapters fill arguments in the common syscall register order.
struct Context {
    std::uint64_t number = 0;
    std::uint64_t arguments[3]{};
    long long result = -1;
};

void dispatch(Context& context);

} // namespace shirley::syscall
