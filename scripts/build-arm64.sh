#!/usr/bin/env sh
# 在 ARM64 建置目錄產生並編譯核心。
set -eu
# 優先使用 Homebrew LLVM，否則使用系統 clang。
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/arm64"
llvm_prefix=$(brew --prefix llvm 2>/dev/null || dirname "$(command -v clang)")
# 以 ARM64 裸機工具鏈設定 CMake 並執行建置。
cmake -S "$root" -B "$build" -G Ninja -DSHIRLEY_TARGET=arm64 -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchain-arm64.cmake" -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" -DCMAKE_ASM_COMPILER="$llvm_prefix/bin/clang" -DSHIRLEY_LLD="$llvm_prefix/bin/ld.lld" -DSHIRLEY_OBJCOPY="$llvm_prefix/bin/llvm-objcopy"
cmake --build "$build"
echo "$build/shirley-kernel"
