#!/usr/bin/env sh
# 建置 QEMU ARM64 目標（arch/arm64 加上 platform/qemu_arm64）。
# Build the QEMU ARM64 target: arch/arm64 plus platform/qemu_arm64.
set -eu
exec "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build.sh" arm64
