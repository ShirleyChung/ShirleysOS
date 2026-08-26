#!/usr/bin/env sh
# 建置 x86_64 UEFI 目標，並在 OVMF 韌體下以 QEMU 啟動。
# Build the x86_64 UEFI target and boot it under QEMU with OVMF firmware.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }
firmware=${SHIRLEY_UEFI_FIRMWARE:-$("$root/scripts/find-uefi-firmware.sh" x86_64)}
echo "Building ShirleyOS x86_64 (UEFI)..."
esp=$("$root/scripts/build.sh" x86_64_uefi)
[ -f "$esp/EFI/BOOT/BOOTX64.EFI" ] || { echo "The EFI system partition was not created: $esp" >&2; exit 1; }
echo "Firmware: $firmware"
echo "Launching QEMU..."
# QEMU 直接把 esp 目錄當成 FAT 磁碟區提供給韌體，因此不需要真的產生 FAT 映像。
# QEMU serves the esp directory to the firmware as a FAT volume, so no real FAT
# image has to be produced.
exec qemu-system-x86_64 -machine q35 -m 512M -nographic -monitor none -serial stdio \
  -bios "$firmware" -drive "format=raw,file=fat:rw:$esp"
