#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x86_64"
llvm_prefix=$(brew --prefix llvm 2>/dev/null || dirname "$(command -v clang)")
cmake -S "$root" -B "$build" -G Ninja -DSHIRLEY_TARGET=x86_64 -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchain-x86_64.cmake" -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" -DCMAKE_ASM_COMPILER="$llvm_prefix/bin/clang" -DSHIRLEY_LLD="$llvm_prefix/bin/ld.lld" -DSHIRLEY_OBJCOPY="$llvm_prefix/bin/llvm-objcopy"
cmake --build "$build"
echo "$build/shirley-x86_64.img"
