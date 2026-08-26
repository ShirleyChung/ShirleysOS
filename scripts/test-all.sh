#!/usr/bin/env sh
# 建置所有目標，並以 QEMU 執行可模擬目標的整合啟動測試。
# Build every target and run integration boot tests under QEMU for the ones
# that can be emulated.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
echo "ShirleyOS Integration Tests"
command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "Missing qemu-system-aarch64. Install with: brew install qemu" >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }

# 主機測試涵蓋與架構無關的核心元件、韌體資料格式解析與開機載入器邏輯。
# The host tests cover the architecture-neutral kernel components, the firmware
# data format parsers, and the boot loader logic.
printf "[host]        test  ... "
host_build="$root/build/host"
cmake -S "$root" -B "$host_build" >/dev/null 2>&1
cmake --build "$host_build" >/dev/null 2>&1
ctest --test-dir "$host_build" --output-on-failure >/dev/null
echo PASS

# 逐一建置可模擬的目標，啟動後確認核心問候訊息出現。
# Build each emulatable target in turn and confirm the kernel's greeting
# actually appears after boot.
for target in arm64 x86_64 arm64_uefi x86_64_uefi; do
  printf "[%-11s] build ... " "$target"
  artifact=$(sh "$root/scripts/build.sh" "$target" | tail -n 1)
  echo PASS

  # UEFI 目標需要韌體映像；找不到就跳過開機測試而不是讓整份測試失敗。
  # A UEFI target needs a firmware image. If none is present the boot test is
  # skipped rather than failing the whole run.
  firmware=""
  case "$target" in
    *_uefi)
      firmware=${SHIRLEY_UEFI_FIRMWARE:-$(sh "$root/scripts/find-uefi-firmware.sh" "${target%_uefi}" 2>/dev/null || true)}
      if [ -z "$firmware" ]; then
        printf "[%-11s] boot  ... SKIP (no UEFI firmware installed)\n" "$target"
        continue
      fi
      ;;
  esac

  printf "[%-11s] boot  ... " "$target"
  qemu=qemu-system-x86_64
  case "$target" in arm64*) qemu=qemu-system-aarch64 ;; esac
  python3 - "$qemu" "$artifact" "$target" "$firmware" <<'PY'
import subprocess, sys
qemu, artifact, target, firmware = sys.argv[1:]
args = [qemu, '-m', '512M', '-nographic', '-monitor', 'none', '-serial', 'stdio']
if target == 'arm64':
    args += ['-machine', 'virt', '-cpu', 'cortex-a72', '-kernel', artifact]
elif target == 'x86_64':
    args += ['-drive', 'format=raw,file=' + artifact]
elif target == 'arm64_uefi':
    args += ['-machine', 'virt', '-cpu', 'cortex-a72', '-bios', firmware,
             '-drive', 'format=raw,file=fat:rw:' + artifact]
else:
    args += ['-machine', 'q35',
             '-drive', 'if=pflash,format=raw,readonly=on,file=' + firmware,
             '-drive', 'format=raw,file=fat:rw:' + artifact]
# UEFI 韌體本身要花數秒才會交出控制權，因此給它比較長的時間。
# UEFI firmware needs several seconds before it hands over control, so it gets
# a longer timeout.
timeout = 30 if target.endswith('_uefi') else 8
p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
try:
    out, _ = p.communicate(timeout=timeout)
except subprocess.TimeoutExpired:
    p.kill(); out, _ = p.communicate()
if "Hello! Shirley's OS." not in out:
    print(out, file=sys.stderr)
    raise SystemExit(1)
if target.endswith('_uefi'):
    stages = [
        '[uefi] entered EFI application',
        '[uefi] kernel ELF read',
        '[uefi] kernel ELF loaded',
        '[uefi] boot services exited',
        '[uefi] entering kernel',
        'ShirleyOS booting...',
    ]
    position = -1
    for stage in stages:
        next_position = out.find(stage, position + 1)
        if next_position < 0:
            print('Missing boot stage: ' + stage, file=sys.stderr)
            print(out, file=sys.stderr)
            raise SystemExit(1)
        position = next_position
# 未處理的 CPU 例外會印出診斷訊息，這代表開機路徑其實已經失敗。
# An unhandled CPU exception prints a diagnostic, which means the boot path
# actually failed even though the greeting was reached.
if "CPU exception" in out:
    print(out, file=sys.stderr)
    raise SystemExit(1)
PY
  echo PASS
done

# Apple Silicon 沒有模擬器，只驗證能建置出核心。
# There is no Apple Silicon emulator, so only the build is verified.
printf "[apple_silicon] build ... "
sh "$root/scripts/build.sh" apple_silicon >/dev/null
echo PASS

echo "All tests passed."
