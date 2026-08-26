#!/usr/bin/env sh
# 建置 x86_64 映像並在 QEMU 中啟動。
# Build the x86_64 disk image and boot it under QEMU.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }
echo "Building ShirleyOS x86_64..."
image=$("$root/scripts/build.sh" x86_64)
[ -f "$image" ] || { echo "Disk image was not created: $image" >&2; exit 1; }
echo "Launching QEMU..."
exec qemu-system-x86_64 -m 512M -nographic -monitor none -serial stdio -drive "format=raw,file=$image"
