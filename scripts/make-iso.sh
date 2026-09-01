#!/usr/bin/env bash
# Build a bootable UEFI ISO for VMware and other x86_64/ARM64 virtual machines.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
TARGET=${1:-x86_64_uefi}
if [[ "$TARGET" == "arm64_uefi" ]]; then
    DEFAULT_OUTPUT="$ROOT/dist/shirleyos-arm64-uefi.iso"
    EFI_NAME=BOOTAA64.EFI
else
    DEFAULT_OUTPUT="$ROOT/dist/shirleyos-x86_64-uefi.iso"
    EFI_NAME=BOOTX64.EFI
fi
OUTPUT=${2:-$DEFAULT_OUTPUT}
ESP_DEVICE=

if [[ "$TARGET" != "x86_64_uefi" && "$TARGET" != "arm64_uefi" ]]; then
    echo "Supported ISO targets are x86_64_uefi and arm64_uefi: $TARGET" >&2
    exit 2
fi

XORRISO=$(command -v xorriso || true)
if [[ -z "$XORRISO" ]]; then
    echo "Missing xorriso. Install it with: brew install xorriso" >&2
    exit 1
fi

ESP=$(sh "$ROOT/scripts/build.sh" "$TARGET" | tail -n 1)
EFI="$ESP/EFI/BOOT/$EFI_NAME"
KERNEL="$ESP/shirley/kernel.elf"
[[ -f "$EFI" ]] || { echo "Missing UEFI loader: $EFI" >&2; exit 1; }
[[ -f "$KERNEL" ]] || { echo "Missing kernel: $KERNEL" >&2; exit 1; }

mkdir -p "$(dirname "$OUTPUT")"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/shirleyos-iso.XXXXXX")
trap 'if [[ -n "$ESP_DEVICE" ]]; then hdiutil detach "$ESP_DEVICE" >/dev/null 2>&1 || true; fi; rm -rf "$WORK"' EXIT

# Create a real FAT filesystem containing the EFI loader and kernel, then add
# it as the ISO's El Torito UEFI boot image.
hdiutil create -srcfolder "$ESP" -fs MS-DOS -layout NONE -format UDRW -ov \
    -o "$WORK/esp" >/dev/null
ESP_DMG="$WORK/esp.dmg"
ESP_DEVICE=$(hdiutil attach -nomount -readonly "$ESP_DMG" | awk 'NF { print $1; exit }')
[[ -n "$ESP_DEVICE" ]] || { echo "Could not attach temporary EFI image" >&2; exit 1; }
ESP_RAW="$WORK/esp.raw"
disk=$(basename "$ESP_DEVICE")
dd if="/dev/r$disk" of="$ESP_RAW" bs=1m >/dev/null 2>&1
hdiutil detach "$ESP_DEVICE" >/dev/null
ESP_DEVICE=

# xorriso requires the El Torito image to be reachable from the ISO source
# tree, so keep it in a temporary staging directory alongside the ESP files.
ISO_SOURCE="$WORK/iso-source"
mkdir -p "$ISO_SOURCE"
cp -R "$ESP/." "$ISO_SOURCE/"
cp "$ESP_RAW" "$ISO_SOURCE/esp.raw"

"$XORRISO" -as mkisofs \
    -R -J -V SHIRLEYOS \
    -o "$OUTPUT" \
    -eltorito-alt-boot -e esp.raw -no-emul-boot \
    -isohybrid-gpt-basdat \
    "$ISO_SOURCE"

echo "Created bootable ShirleyOS ISO: $OUTPUT"
echo "Size: $(du -h "$OUTPUT" | awk '{print $1}')"
