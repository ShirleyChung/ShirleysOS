#!/usr/bin/env sh
# 建置 x86_64 映像並讓 QEMU 等待除錯器連線。
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=$($root/scripts/build-x86_64.sh | tail -n 1)
echo "ShirleyOS waiting for debugger on localhost:1234"
echo "LLDB example: gdb-remote localhost:1234"
exec qemu-system-x86_64 -m 512M -nographic -monitor none -S -gdb tcp::1234 -drive "format=raw,file=$image"
