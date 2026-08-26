#!/usr/bin/env sh
# 建置指定目標並在標準輸出印出產出的成品路徑。
# 用法：scripts/build.sh <arm64|x86_64|apple_silicon>
#
# Build one target and print the resulting artifact path on standard output.
# Usage: scripts/build.sh <arm64|x86_64|apple_silicon>
set -eu
target=${1:?Usage: build.sh <arm64|x86_64|apple_silicon>}

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case "$target" in
  arm64) toolchain=toolchain-arm64.cmake; artifact=shirley-kernel.elf ;;
  apple_silicon) toolchain=toolchain-arm64.cmake; artifact=shirley-kernel.elf ;;
  x86_64) toolchain=toolchain-x86_64.cmake; artifact=shirley-x86_64.img ;;
  *) echo "Unsupported target: $target" >&2; exit 2 ;;
esac

# 優先使用 Homebrew LLVM，否則使用系統 clang。
# Prefer Homebrew's LLVM, and fall back to the system clang.
llvm_prefix=$(brew --prefix llvm 2>/dev/null || dirname "$(command -v clang)")
lld=$(command -v ld.lld 2>/dev/null || true)
if [ -z "$lld" ] && command -v brew >/dev/null 2>&1; then
  lld_prefix=$(brew --prefix lld 2>/dev/null || true)
  [ -x "$lld_prefix/bin/ld.lld" ] && lld="$lld_prefix/bin/ld.lld"
fi
[ -n "$lld" ] || { echo "Missing ld.lld. Install with: brew install lld" >&2; exit 1; }

# Ninja 是選用的；不同產生器使用各自的建置目錄，
# 開發者切換時 CMake 快取才不會互相衝突。
# Ninja is optional. Each generator gets its own build tree so a developer
# switching between them does not hit conflicting CMake caches.
build="$root/build/$target"
if command -v ninja >/dev/null 2>&1; then
  generator=Ninja
else
  generator="Unix Makefiles"
  build="$root/build/$target-make"
fi

cmake -S "$root" -B "$build" -G "$generator" \
  -DSHIRLEY_TARGET="$target" \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/$toolchain" \
  -DCMAKE_C_COMPILER="$llvm_prefix/bin/clang" \
  -DCMAKE_CXX_COMPILER="$llvm_prefix/bin/clang++" \
  -DCMAKE_ASM_COMPILER="$llvm_prefix/bin/clang" \
  -DSHIRLEY_LLD="$lld" \
  -DSHIRLEY_OBJCOPY="$llvm_prefix/bin/llvm-objcopy" >&2
cmake --build "$build" >&2
echo "$build/$artifact"
