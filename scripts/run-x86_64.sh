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
# 主控台的輸入現在有兩條路：序列埠（IRQ4）與 PS/2 鍵盤（IRQ1）。序列埠這條
# 在這個終端機裡就能用，因此預設不開顯示視窗，直接在這裡對 shell 打字。
# QEMU 的 PS/2 按鍵事件來自顯示裝置，所以要試 IRQ1 那條路徑時，設定
# SHIRLEY_DISPLAY=1 開一個視窗，在視窗裡打字。
#
# Console input now has two paths: the serial port on IRQ4 and the PS/2
# keyboard on IRQ1. The serial one works right here in this terminal, so no
# display window is opened by default and the shell is typed at directly. QEMU
# sources PS/2 key events from its display device, so set SHIRLEY_DISPLAY=1 to
# open a window and type there when it is the IRQ1 path being tried.
if [ "${SHIRLEY_DISPLAY:-0}" = 1 ]; then
  echo "Type in the QEMU display window to exercise the IRQ1 keyboard path."
  exec qemu-system-x86_64 -m 512M -monitor none -serial stdio -drive "format=raw,file=$image"
fi
echo "Type here at the shell prompt. Ctrl-A then X exits QEMU."
exec qemu-system-x86_64 -m 512M -nographic -monitor none -serial stdio -drive "format=raw,file=$image"
