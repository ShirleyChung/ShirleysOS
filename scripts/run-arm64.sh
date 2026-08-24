#!/usr/bin/env sh
# 建置 ARM64 核心並在 QEMU virt 機器上啟動。
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "Missing qemu-system-aarch64. Install with: brew install qemu" >&2; exit 1; }
echo "Building ShirleyOS ARM64..."
kernel=$("$root/scripts/build-arm64.sh")
[ -f "$kernel" ] || { echo "Kernel artifact was not created: $kernel" >&2; exit 1; }
echo "Launching QEMU..."
exec qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 512M -nographic -monitor none -serial stdio -kernel "$kernel"
