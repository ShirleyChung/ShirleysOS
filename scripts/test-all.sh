#!/usr/bin/env sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
echo "ShirleyOS Integration Tests"
command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "Missing qemu-system-aarch64. Install with: brew install qemu" >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }
for target in arm64 x86_64; do
  printf "[%s]  build ... " "$target"
  image=$($root/scripts/build-$target.sh | tail -n 1)
  echo PASS
  printf "[%s]  boot  ... " "$target"
  qemu="qemu-system-$target"
  [ "$target" = arm64 ] && qemu=qemu-system-aarch64
  output=$(python3 - "$qemu" "$image" "$target" <<'PY'
import subprocess, sys
qemu, image, target = sys.argv[1:]
args = [qemu, '-m', '512M', '-nographic', '-monitor', 'none', '-serial', 'stdio']
if target == 'arm64': args += ['-machine', 'virt', '-cpu', 'cortex-a72', '-kernel', image]
else: args += ['-drive', 'format=raw,file=' + image]
p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
try:
    out, _ = p.communicate(timeout=8)
except subprocess.TimeoutExpired:
    p.kill(); out, _ = p.communicate()
if "Hello! Shirley's OS." not in out:
    print(out, file=sys.stderr)
    raise SystemExit(1)
PY
  )
  echo PASS
done
echo "All tests passed."
