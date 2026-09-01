#include "shirley/syscall.hpp"

#include "shirley/arch.hpp"
#include "shirley/process.hpp"

namespace shirley::syscall {
namespace {

// 把 ABI 傳來的整數引數還原成指標。系統呼叫執行時，使用者的位址空間仍然生效
// （核心也映射在其中），因此使用者指標可以直接取用——這與現有的寫入路徑一致，
// 尚未加入 copy-from-user 的檢查。
//
// Recover a pointer from an integer argument the ABI passed. A syscall runs
// with the user's address space still active (the kernel is mapped in it too),
// so a user pointer can be used directly; this matches the existing write path
// and does not yet add copy-from-user checks.
void* as_pointer(std::uint64_t value) {
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
}

} // namespace

void dispatch(Context& context) {
    switch (static_cast<Number>(context.number)) {
    case Number::Write:
        context.result = process::write(static_cast<int>(context.arguments[0]),
                                        as_pointer(context.arguments[1]),
                                        static_cast<std::size_t>(context.arguments[2]));
        return;
    case Number::Read:
        context.result = process::read(static_cast<int>(context.arguments[0]),
                                       as_pointer(context.arguments[1]),
                                       static_cast<std::size_t>(context.arguments[2]));
        return;
    case Number::Open:
        context.result = process::open(static_cast<const char*>(as_pointer(context.arguments[0])),
                                       context.arguments[1]);
        return;
    case Number::Close:
        context.result = process::close(static_cast<int>(context.arguments[0]));
        return;
    case Number::Exit:
        // 先關掉行程還開著的描述子，再把控制權交回啟動它的核心程式碼。
        // exit_userspace 不返回，因此這裡不會走到設定 result 那步。
        //
        // Close whatever the process left open, then hand control back to the
        // kernel code that started it. exit_userspace does not return, so
        // nothing after it runs.
        process::teardown();
        arch::exit_userspace(static_cast<int>(context.arguments[0]));
        return;
    default:
        context.result = -1;
        return;
    }
}

} // namespace shirley::syscall
