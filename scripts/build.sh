#!/usr/bin/env sh
# 建置指定目標並在標準輸出印出產出的成品路徑。
# 用法：scripts/build.sh <arm64|x86_64|apple_silicon>
#
# Build one target and print the resulting artifact path on standard output.
# Usage: scripts/build.sh <arm64|x86_64|apple_silicon>
set -eu
target=${1:?Usage: build.sh <arm64|x86_64|apple_silicon>}

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake_bin=$(command -v cmake 2>/dev/null || true)
if [ -z "$cmake_bin" ] && [ -x "/c/Program Files/CMake/bin/cmake.exe" ]; then
  cmake_bin="/c/Program Files/CMake/bin/cmake.exe"
fi
[ -n "$cmake_bin" ] || { echo "Missing cmake. Install CMake and ensure cmake is in PATH." >&2; exit 1; }
# UEFI 目標的成品是 EFI 系統分割區目錄，QEMU 會直接把它當成 FAT 磁碟區。
# The artifact of a UEFI target is the EFI system partition directory, which
# QEMU serves directly as a FAT volume.
case "$target" in
  arm64) toolchain=toolchain-arm64.cmake; artifact=shirley-kernel.elf ;;
  arm64_uefi) toolchain=toolchain-arm64.cmake; artifact=esp ;;
  apple_silicon) toolchain=toolchain-arm64.cmake; artifact=shirley-kernel.elf ;;
  x86_64) toolchain=toolchain-x86_64.cmake; artifact=shirley-x86_64.img ;;
  x86_64_uefi) toolchain=toolchain-x86_64.cmake; artifact=esp ;;
  *) echo "Unsupported target: $target" >&2; exit 2 ;;
esac

# 優先使用 Homebrew LLVM，否則使用系統 clang。
# Prefer Homebrew's LLVM, and fall back to the system clang.
if command -v brew >/dev/null 2>&1; then
  llvm_prefix=$(brew --prefix llvm 2>/dev/null || true)
else
  llvm_prefix=
fi
if [ -n "$llvm_prefix" ]; then
  cc="$llvm_prefix/bin/clang"
  cxx="$llvm_prefix/bin/clang++"
  asm="$llvm_prefix/bin/clang"
  objcopy="$llvm_prefix/bin/llvm-objcopy"
else
  cc=
  cxx=
  objcopy=
  if [ -x "/c/Program Files/LLVM/bin/clang.exe" ] && [ -x "/c/Program Files/LLVM/bin/clang++.exe" ]; then
    cc="/c/Program Files/LLVM/bin/clang.exe"
    cxx="/c/Program Files/LLVM/bin/clang++.exe"
    if [ -x "/c/Program Files/LLVM/bin/llvm-objcopy.exe" ]; then
      objcopy="/c/Program Files/LLVM/bin/llvm-objcopy.exe"
    fi
  else
    cc=$(command -v clang 2>/dev/null || true)
    cxx=$(command -v clang++ 2>/dev/null || true)
    objcopy=$(command -v llvm-objcopy 2>/dev/null || true)
  fi
  [ -n "$cc" ] || { echo "Missing clang. Install LLVM and ensure clang is in PATH." >&2; exit 1; }
  [ -n "$cxx" ] || { echo "Missing clang++. Install LLVM and ensure clang++ is in PATH." >&2; exit 1; }
  asm="$cc"
  if [ -z "$objcopy" ] && [ -x "/c/Program Files/LLVM/bin/llvm-objcopy.exe" ]; then
    objcopy="/c/Program Files/LLVM/bin/llvm-objcopy.exe"
  fi
  [ -n "$objcopy" ] || { echo "Missing llvm-objcopy. Install LLVM and ensure llvm-objcopy is in PATH." >&2; exit 1; }
fi
lld=$(command -v ld.lld 2>/dev/null || true)
if [ -z "$lld" ] && [ -x "/c/Program Files/LLVM/bin/ld.lld.exe" ]; then
  lld="/c/Program Files/LLVM/bin/ld.lld.exe"
fi
if [ -z "$lld" ] && command -v brew >/dev/null 2>&1; then
  lld_prefix=$(brew --prefix lld 2>/dev/null || true)
  [ -x "$lld_prefix/bin/ld.lld" ] && lld="$lld_prefix/bin/ld.lld"
fi
[ -n "$lld" ] || { echo "Missing ld.lld. Install LLVM/lld and ensure ld.lld is in PATH." >&2; exit 1; }

# Ninja 是選用的；不同產生器使用各自的建置目錄，
# 開發者切換時 CMake 快取才不會互相衝突。
# Ninja is optional. Each generator gets its own build tree so a developer
# switching between them does not hit conflicting CMake caches.
build="$root/build/$target"
ninja_bin=$(command -v ninja 2>/dev/null || true)
if [ -z "$ninja_bin" ] && [ -x "/c/Users/shirl/AppData/Local/Microsoft/WinGet/Packages/Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe/ninja.exe" ]; then
  ninja_bin="/c/Users/shirl/AppData/Local/Microsoft/WinGet/Packages/Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe/ninja.exe"
fi
if [ -n "$ninja_bin" ]; then
  generator=Ninja
  make_program_arg="-DCMAKE_MAKE_PROGRAM=$ninja_bin"
else
  generator="Unix Makefiles"
  build="$root/build/$target-make"
  make_program_arg=
fi

# 這個腳本是跨工具鏈入口；為了避免舊快取把編譯器或工具鏈鎖在錯誤值，
# 每次都清掉 configure cache，再重新產生。
# This script is a cross-toolchain entrypoint. To avoid stale cache pinning
# wrong compiler/toolchain values, always recreate the configure cache.
rm -f "$build/CMakeCache.txt"
rm -rf "$build/CMakeFiles"

"$cmake_bin" -S "$root" -B "$build" -G "$generator" \
  -DSHIRLEY_TARGET="$target" \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/$toolchain" \
  -DCMAKE_C_COMPILER="$cc" \
  -DCMAKE_CXX_COMPILER="$cxx" \
  -DCMAKE_ASM_COMPILER="$asm" \
  -DSHIRLEY_LLD="$lld" \
  ${make_program_arg:+$make_program_arg} \
  -DSHIRLEY_OBJCOPY="$objcopy" >&2
"$cmake_bin" --build "$build" >&2
echo "$build/$artifact"
