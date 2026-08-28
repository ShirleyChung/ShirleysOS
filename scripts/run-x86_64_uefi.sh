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
# 序列埠的輸入路徑在這個終端機裡就能用，因此預設不開顯示視窗；PS/2 的按鍵
# 事件來自顯示裝置，要試 IRQ1 那條路徑時設定 SHIRLEY_DISPLAY=1。
#
# The serial input path works right here in this terminal, so no display window
# is opened by default. PS/2 key events come from the display device, so set
# SHIRLEY_DISPLAY=1 when it is the IRQ1 path being tried.
if [ "${SHIRLEY_DISPLAY:-0}" = 1 ]; then
  echo "Type in the QEMU display window to exercise the IRQ1 keyboard path."
  exec qemu-system-x86_64 -machine q35 -m 512M -monitor none -serial stdio \
    -drive "if=pflash,format=raw,readonly=on,file=$firmware" \
    -drive "format=raw,file=fat:rw:$esp"
fi
echo "Type here at the shell prompt. Ctrl-A then X exits QEMU."
exec qemu-system-x86_64 -machine q35 -m 512M -nographic -monitor none -serial stdio \
  -drive "if=pflash,format=raw,readonly=on,file=$firmware" \
  -drive "format=raw,file=fat:rw:$esp"
