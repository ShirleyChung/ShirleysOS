#include "shirley/syscall.hpp"

#include "shirley/console.hpp"

namespace shirley::syscall {

void dispatch(Context& context) {
    switch (static_cast<Number>(context.number)) {
        case Number::Write: {
            if (context.arguments[0] != 1) {
                context.result = -1;
                return;
            }
            if (context.arguments[1] == 0 && context.arguments[2] != 0) {
                context.result = -1;
                return;
            }
            console::write(
                reinterpret_cast<const char*>(static_cast<std::uintptr_t>(context.arguments[1])),
                static_cast<std::size_t>(context.arguments[2]));
            context.result = static_cast<long long>(context.arguments[2]);
            return;
        }
        case Number::Exit:
            // Process teardown is added with the user process manager. For
            // now, returning keeps the syscall ABI testable without hiding a
            // failed write behind a non-returning path.
            context.result = static_cast<long long>(context.arguments[0]);
            return;
        default:
            context.result = -1;
            return;
    }
}

} // namespace shirley::syscall
