#!/usr/bin/env sh
# 建置 x86_64 映像並讓 QEMU 等待除錯器連線。
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }
image=$("$root/scripts/build-x86_64.sh")
[ -f "$image" ] || { echo "Disk image was not created: $image" >&2; exit 1; }
echo "ShirleyOS waiting for debugger on localhost:1234"
echo "LLDB example: gdb-remote localhost:1234"
exec qemu-system-x86_64 -m 512M -nographic -monitor none -S -gdb tcp::1234 -drive "format=raw,file=$image"
