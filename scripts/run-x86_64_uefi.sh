#!/usr/bin/env sh
# 建置 x86_64 UEFI 目標，並在 OVMF 韌體下以 QEMU 啟動。
# Build the x86_64 UEFI target and boot it under QEMU with OVMF firmware.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }
firmware=${SHIRLEY_UEFI_FIRMWARE:-$(sh "$root/scripts/find-uefi-firmware.sh" x86_64)}
echo "Building ShirleyOS x86_64 (UEFI)..."
esp=$(sh "$root/scripts/build.sh" x86_64_uefi)
[ -f "$esp/EFI/BOOT/BOOTX64.EFI" ] || { echo "The EFI system partition was not created: $esp" >&2; exit 1; }
echo "Firmware: $firmware"
echo "Launching QEMU..."
# QEMU 直接把 esp 目錄當成 FAT 磁碟區提供給韌體，因此不需要真的產生 FAT 映像。
# QEMU serves the esp directory to the firmware as a FAT volume, so no real FAT
# image has to be produced.
#
# 鍵盤事件來自顯示裝置，因此預設開著顯示視窗，IRQ1 才會真的被觸發；
# SHIRLEY_HEADLESS=1 可以回到純文字模式，但那時沒有鍵盤輸入。
#
# Keyboard events come from the display device, so the display window is open
# by default and IRQ1 genuinely fires. SHIRLEY_HEADLESS=1 returns to the plain
# text mode, which has no keyboard input.
if [ "${SHIRLEY_HEADLESS:-0}" = 1 ]; then
  exec qemu-system-x86_64 -machine q35 -m 512M -nographic -monitor none -serial stdio \
    -drive "if=pflash,format=raw,readonly=on,file=$firmware" \
    -drive "format=raw,file=fat:rw:$esp"
fi
echo "Type in the QEMU display window to exercise the IRQ1 keyboard path."
exec qemu-system-x86_64 -machine q35 -m 512M -monitor none -serial stdio \
  -drive "if=pflash,format=raw,readonly=on,file=$firmware" \
  -drive "format=raw,file=fat:rw:$esp"
