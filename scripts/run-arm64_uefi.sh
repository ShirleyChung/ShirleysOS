#!/usr/bin/env sh
# 建置 ARM64 UEFI 目標，並在 EDK2 韌體下以 QEMU virt 啟動。
# Build the ARM64 UEFI target and boot it on QEMU virt with EDK2 firmware.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "Missing qemu-system-aarch64. Install with: brew install qemu" >&2; exit 1; }
firmware=${SHIRLEY_UEFI_FIRMWARE:-$(sh "$root/scripts/find-uefi-firmware.sh" arm64)}
echo "Building ShirleyOS ARM64 (UEFI)..."
esp=$(sh "$root/scripts/build.sh" arm64_uefi)
[ -f "$esp/EFI/BOOT/BOOTAA64.EFI" ] || { echo "The EFI system partition was not created: $esp" >&2; exit 1; }
echo "Firmware: $firmware"
echo "Launching QEMU..."
# QEMU 直接把 esp 目錄當成 FAT 磁碟區提供給韌體，因此不需要真的產生 FAT 映像。
# QEMU serves the esp directory to the firmware as a FAT volume, so no real FAT
# image has to be produced.
exec qemu-system-aarch64 -machine virt -cpu cortex-a72 -m 512M -nographic -monitor none -serial stdio \
  -bios "$firmware" -drive "format=raw,file=fat:rw:$esp"
