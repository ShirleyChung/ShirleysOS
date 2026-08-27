#pragma once

#include <cstdint>

namespace shirley::syscall {

enum class Number : std::uint64_t { Write = 1, Exit = 2 };

// Architecture adapters fill arguments in the common syscall register order.
struct Context {
    std::uint64_t number = 0;
    std::uint64_t arguments[3]{};
    long long result = -1;
};

void dispatch(Context& context);

} // namespace shirley::syscall
