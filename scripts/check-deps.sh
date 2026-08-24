#!/usr/bin/env sh
set -u
echo "ShirleyOS development environment"
missing=0
check() { if command -v "$1" >/dev/null 2>&1; then echo "[OK] $1"; else echo "[MISSING] $1"; missing=1; fi; }
for tool in brew cmake ninja clang lld qemu-system-aarch64 qemu-system-x86_64; do check "$tool"; done
if [ "$missing" -eq 0 ]; then echo "Environment ready."; exit 0; fi
echo ""
echo "Install missing host dependencies with:"
echo "  brew install cmake ninja llvm qemu"
exit 1
