#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kernel=$($root/scripts/build-arm64.sh | tail -n 1)
echo "ShirleyOS waiting for debugger on localhost:1234"
echo "LLDB example: gdb-remote localhost:1234"
exec qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 512M -nographic -monitor none -serial stdio -S -gdb tcp::1234 -kernel "$kernel"
