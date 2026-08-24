#!/usr/bin/env sh
# 檢查建置、模擬與測試所需的本機工具。
set -u
echo "ShirleyOS development environment"
# missing 記錄是否有任何依賴尚未安裝。
missing=0
check() { if command -v "$1" >/dev/null 2>&1; then echo "[OK] $1"; else echo "[MISSING] $1"; missing=1; fi; }
for tool in brew cmake ninja clang lld qemu-system-aarch64 qemu-system-x86_64; do check "$tool"; done
if [ "$missing" -eq 0 ]; then echo "Environment ready."; exit 0; fi
echo ""
echo "Install missing host dependencies with:"
echo "  brew install cmake ninja llvm qemu"
exit 1
