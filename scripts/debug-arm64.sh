#!/usr/bin/env sh
# 建置 ARM64 核心並讓 QEMU 等待除錯器連線。
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "Missing qemu-system-aarch64. Install with: brew install qemu" >&2; exit 1; }
kernel=$("$root/scripts/build-arm64.sh")
[ -f "$kernel" ] || { echo "Kernel artifact was not created: $kernel" >&2; exit 1; }
echo "ShirleyOS waiting for debugger on localhost:1234"
echo "LLDB example: gdb-remote localhost:1234"
exec qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 512M -nographic -monitor none -serial stdio -S -gdb tcp::1234 -kernel "$kernel"
