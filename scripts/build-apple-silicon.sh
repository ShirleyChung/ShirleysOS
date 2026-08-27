#!/usr/bin/env sh
# 建置 Apple Silicon 目標（arch/arm64 加上 platform/apple_silicon）。
# QEMU 沒有 Apple Silicon 機器模型，這個目標目前只驗證建置，
# 實機執行需要 m1n1 之類的載入器，屬於 M8 里程碑。
#
# Build the Apple Silicon target: arch/arm64 plus platform/apple_silicon.
# QEMU has no Apple Silicon machine model, so this target is build-verified
# only; running on real hardware needs an m1n1-style loader and is M8.
set -eu
exec "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build.sh" apple_silicon
