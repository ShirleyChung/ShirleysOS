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
# QEMU 的 PS/2 鍵盤事件來自顯示裝置，因此 -nographic 之下按鍵永遠不會產生
# IRQ1。預設開啟顯示視窗讓鍵盤真的可以用，主控台輸出仍然走序列埠到終端機；
# 設定 SHIRLEY_HEADLESS=1 可以回到純文字模式，但那時只有輸出、沒有鍵盤。
#
# QEMU sources PS/2 keyboard events from the display device, so under
# -nographic a keypress can never raise IRQ1. The default opens a display
# window so the keyboard genuinely works, while console output still travels
# over the serial port to this terminal. Set SHIRLEY_HEADLESS=1 for the plain
# text mode instead, which is output-only with no keyboard.
if [ "${SHIRLEY_HEADLESS:-0}" = 1 ]; then
  exec qemu-system-x86_64 -m 512M -nographic -monitor none -serial stdio -drive "format=raw,file=$image"
fi
echo "Type in the QEMU display window to exercise the IRQ1 keyboard path."
exec qemu-system-x86_64 -m 512M -monitor none -serial stdio -drive "format=raw,file=$image"
