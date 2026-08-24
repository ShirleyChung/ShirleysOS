#!/usr/bin/env sh
# 在 x86_64 建置目錄產生核心與磁碟映像。
set -eu
# 優先使用 Homebrew LLVM，否則使用系統 clang。
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/x86_64"
llvm_prefix=$(brew --prefix llvm 2>/dev/null || dirname "$(command -v clang)")
lld=$(command -v ld.lld 2>/dev/null || true)
if [ -z "$lld" ] && command -v brew >/dev/null 2>&1; then
  lld_prefix=$(brew --prefix lld 2>/dev/null || true)
  [ -x "$lld_prefix/bin/ld.lld" ] && lld="$lld_prefix/bin/ld.lld"
fi
[ -n "$lld" ] || { echo "Missing ld.lld. Install with: brew install lld" >&2; exit 1; }
# Ninja is optional; keep generators in separate build trees so CMake caches do
# not conflict when a developer switches between them.
if command -v ninja >/dev/null 2>&1; then
  generator=Ninja
else
  generator="Unix Makefiles"
  build="$root/build/x86_64-make"
fi
# 以 x86_64 裸機工具鏈設定 CMake 並執行建置。
cmake -S "$root" -B "$build" -G "$generator" -DSHIRLEY_TARGET=x86_64 -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchain-x86_64.cmake" -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" -DCMAKE_ASM_COMPILER="$llvm_prefix/bin/clang" -DSHIRLEY_LLD="$lld" -DSHIRLEY_OBJCOPY="$llvm_prefix/bin/llvm-objcopy" >&2
cmake --build "$build" >&2
echo "$build/shirley-x86_64.img"
