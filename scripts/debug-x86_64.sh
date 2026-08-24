#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
image=$($root/scripts/build-x86_64.sh | tail -n 1)
echo "ShirleyOS waiting for debugger on localhost:1234"
echo "LLDB example: gdb-remote localhost:1234"
exec qemu-system-x86_64 -m 512M -nographic -monitor none -S -gdb tcp::1234 -drive "format=raw,file=$image"
