#!/usr/bin/env sh
# 找出本機可用的 UEFI 韌體映像並印出路徑。
# 用法：scripts/find-uefi-firmware.sh <x86_64|arm64>
#
# Locate a usable UEFI firmware image on this machine and print its path.
# Usage: scripts/find-uefi-firmware.sh <x86_64|arm64>
set -eu
architecture=${1:?Usage: find-uefi-firmware.sh <x86_64|arm64>}

case "$architecture" in
  # Homebrew 的 qemu 會一併安裝 EDK2 建置出來的韌體；不同版本檔名不同，
  # 因此逐一嘗試已知的名稱。
  # Homebrew's qemu ships firmware built from EDK2. The file name varies
  # between versions, so each known name is tried in turn.
  x86_64) names="edk2-x86_64-code.fd OVMF_CODE.fd OVMF.fd" ;;
  arm64) names="edk2-aarch64-code.fd AAVMF_CODE.fd QEMU_EFI.fd" ;;
  *) echo "Unsupported architecture: $architecture" >&2; exit 2 ;;
esac

directories="$(brew --prefix qemu 2>/dev/null || true)/share/qemu
/usr/local/share/qemu
/opt/homebrew/share/qemu
/usr/share/qemu
/usr/share/OVMF
/usr/share/AAVMF
/usr/share/edk2/x64
/usr/share/edk2/aarch64"

for directory in $directories; do
  for name in $names; do
    if [ -f "$directory/$name" ]; then
      echo "$directory/$name"
      exit 0
    fi
  done
done

echo "No UEFI firmware found for $architecture." >&2
echo "It ships with QEMU; install it with: brew install qemu" >&2
echo "Or point SHIRLEY_UEFI_FIRMWARE at a firmware image yourself." >&2
exit 1
