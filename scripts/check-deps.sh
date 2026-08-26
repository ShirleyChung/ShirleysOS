#!/usr/bin/env sh
# 檢查建置、模擬與測試所需的本機工具。
# Check for the host tools needed to build, emulate, and test.
set -u
echo "ShirleyOS development environment"
# missing 記錄是否有任何依賴尚未安裝。
# missing records whether any dependency is still absent.
missing=0
check() { if command -v "$1" >/dev/null 2>&1; then echo "[OK] $1"; else echo "[MISSING] $1"; missing=1; fi; }
for tool in brew cmake clang qemu-system-aarch64 qemu-system-x86_64; do check "$tool"; done
# ld.lld 可能來自獨立的 Homebrew 套件，不一定在 PATH 上。
# ld.lld may come from a separate Homebrew package and need not be on PATH.
if command -v ld.lld >/dev/null 2>&1 || { command -v brew >/dev/null 2>&1 && [ -x "$(brew --prefix lld 2>/dev/null)/bin/ld.lld" ]; }; then
  echo "[OK] ld.lld"
else
  echo "[MISSING] ld.lld"
  missing=1
fi
if [ "$missing" -eq 0 ]; then echo "Environment ready."; exit 0; fi
echo ""
echo "Install missing host dependencies with:"
echo "  brew install cmake llvm lld qemu"
exit 1
