#!/usr/bin/env sh
# 建置所有目標，並以 QEMU 執行可模擬目標的整合啟動測試。
# Build every target and run integration boot tests under QEMU for the ones
# that can be emulated.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
echo "ShirleyOS Integration Tests"
command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "Missing qemu-system-aarch64. Install with: brew install qemu" >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }

# 主機測試涵蓋與架構無關的核心元件與韌體資料格式解析。
# The host tests cover the architecture-neutral kernel components and the
# firmware data format parsers.
printf "[host]   test  ... "
host_build="$root/build/host"
cmake -S "$root" -B "$host_build" >/dev/null 2>&1
cmake --build "$host_build" >/dev/null 2>&1
ctest --test-dir "$host_build" --output-on-failure >/dev/null
echo PASS

# 逐一建置可模擬的目標，啟動後確認核心問候訊息出現。
# Build each emulatable target in turn and confirm the kernel's greeting
# actually appears after boot.
for target in arm64 x86_64; do
  printf "[%s]  build ... " "$target"
  artifact=$("$root/scripts/build.sh" "$target" | tail -n 1)
  echo PASS
  printf "[%s]  boot  ... " "$target"
  qemu="qemu-system-$target"
  [ "$target" = arm64 ] && qemu=qemu-system-aarch64
  python3 - "$qemu" "$artifact" "$target" <<'PY'
import subprocess, sys
qemu, artifact, target = sys.argv[1:]
args = [qemu, '-m', '512M', '-nographic', '-monitor', 'none', '-serial', 'stdio']
if target == 'arm64': args += ['-machine', 'virt', '-cpu', 'cortex-a72', '-kernel', artifact]
else: args += ['-drive', 'format=raw,file=' + artifact]
p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
try:
    out, _ = p.communicate(timeout=8)
except subprocess.TimeoutExpired:
    p.kill(); out, _ = p.communicate()
if "Hello! Shirley's OS." not in out:
    print(out, file=sys.stderr)
    raise SystemExit(1)
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
printf "[apple]  build ... "
"$root/scripts/build.sh" apple_silicon >/dev/null
echo PASS

echo "All tests passed."
