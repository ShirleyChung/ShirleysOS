#!/usr/bin/env sh
# Build the x86_64 UEFI target and wait for a debugger on localhost:1234.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }
firmware=${SHIRLEY_UEFI_FIRMWARE:-$(sh "$root/scripts/find-uefi-firmware.sh" x86_64)}
esp=$(sh "$root/scripts/build.sh" x86_64_uefi)
[ -f "$esp/EFI/BOOT/BOOTX64.EFI" ] || { echo "The EFI system partition was not created: $esp" >&2; exit 1; }
echo "ShirleyOS UEFI waiting for debugger on localhost:1234"
echo "Kernel symbols: $root/build/x86_64_uefi/shirley-kernel.elf"
exec qemu-system-x86_64 -machine q35 -m 512M -nographic -monitor none -serial stdio \
  -S -gdb tcp::1234 -bios "$firmware" -drive "format=raw,file=fat:rw:$esp"
