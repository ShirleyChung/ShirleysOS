#!/usr/bin/env sh
# 建置 QEMU x86_64 目標（arch/x86_64 加上 platform/qemu_x86_64）。
# Build the QEMU x86_64 target: arch/x86_64 plus platform/qemu_x86_64.
set -eu
exec "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build.sh" x86_64
