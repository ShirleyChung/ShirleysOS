#!/usr/bin/env sh
# 建置所有目標，並以 QEMU 執行可模擬目標的整合啟動測試。
# Build every target and run integration boot tests under QEMU for the ones
# that can be emulated.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
echo "ShirleyOS Integration Tests"
command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "Missing qemu-system-aarch64. Install with: brew install qemu" >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }

# 主機測試涵蓋與架構無關的核心元件、韌體資料格式解析、開機載入器邏輯，
# 以及不碰硬體的輸入解碼層。
# The host tests cover the architecture-neutral kernel components, the firmware
# data format parsers, the boot loader logic, and the hardware-independent
# input decoding layer.
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
import os, socket, subprocess, sys, tempfile, threading, time

qemu, artifact, target, firmware = sys.argv[1:]
x86 = not target.startswith('arm64')

# x86 目標會透過 QEMU 監控介面注入真正的按鍵事件，藉此驗證中斷驅動的鍵盤
# 路徑，因此監控介面要接到一個 unix socket，而不是關掉。
#
# The x86 targets inject genuine key events through the QEMU monitor to verify
# the interrupt-driven keyboard path, so the monitor is wired to a unix socket
# rather than turned off.
monitor_path = os.path.join(tempfile.mkdtemp(), 'monitor.sock') if x86 else None
args = [qemu, '-m', '512M', '-nographic', '-serial', 'stdio']
args += ['-monitor', 'unix:%s,server,nowait' % monitor_path] if x86 else ['-monitor', 'none']
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
typed = 'shirley'
collected = []

process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

# QEMU 的輸出必須一邊執行一邊讀，否則按鍵注入之前管線就會塞住。
# QEMU's output has to be drained while it runs, or the pipe fills up before
# any key is injected.
def drain():
    for line in process.stdout:
        collected.append(line)
reader = threading.Thread(target=drain, daemon=True)
reader.start()

def output():
    return ''.join(collected)

def wait_for(needle, limit):
    deadline = time.time() + limit
    while time.time() < deadline:
        if needle in output():
            return True
        if process.poll() is not None:
            return needle in output()
        time.sleep(0.1)
    return needle in output()

def fail(message):
    process.kill()
    reader.join(timeout=2)
    print(message, file=sys.stderr)
    print(output(), file=sys.stderr)
    raise SystemExit(1)

booted = wait_for("Hello! Shirley's OS.", timeout)

keyboard = 'not attempted'
if booted and x86:
    # 開機訊息出現後，計時器已經在跑；等它自己回報滿一秒，證明 IRQ0 會反覆
    # 送達，也就證明 end-of-interrupt 有效。
    #
    # Once the boot messages appear the timer is already running; waiting for
    # it to report a full second proves IRQ0 keeps arriving, and therefore that
    # end-of-interrupt works.
    if not wait_for('[IRQ] timer ticking:', 5):
        fail('Timer IRQ0 never reported a full second of ticks')

    # 逐一注入按鍵。每個字元都必須回顯，重複輸入仍然有效就代表 IRQ1 的
    # end-of-interrupt 沒有漏掉。
    #
    # Inject the keys one at a time. Every character has to be echoed back, and
    # input that keeps working proves no end-of-interrupt was missed on IRQ1.
    monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connected = False
    for _ in range(50):
        try:
            monitor.connect(monitor_path)
            connected = True
            break
        except OSError:
            time.sleep(0.1)
    if not connected:
        keyboard = 'SKIP (QEMU monitor socket unavailable)'
    else:
        baseline = len(output())
        for character in typed:
            monitor.sendall(('sendkey %s\n' % character).encode())
            time.sleep(0.15)
        monitor.close()
        deadline = time.time() + 5
        while time.time() < deadline and typed not in output()[baseline:]:
            time.sleep(0.1)
        if typed not in output()[baseline:]:
            fail('Keys sent to the PS/2 controller were not echoed by the IRQ1 handler')
        keyboard = 'PASS'

process.kill()
reader.join(timeout=2)
out = output()

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
# 中斷子系統的每一段都必須自己回報上線。
# Every stage of the interrupt subsystem has to report itself online.
stages = ['[IRQ] IDT initialized', '[IRQ] PIC remapped 0x20/0x28',
          '[IRQ] PIT timer enabled on IRQ0', '[IRQ] keyboard IRQ enabled'] if x86 else \
         ['[IRQ] EL1 exception vector table initialized']
for stage in stages:
    if stage not in out:
        print('Missing interrupt stage: ' + stage, file=sys.stderr)
        print(out, file=sys.stderr)
        raise SystemExit(1)
# 未處理的 CPU 例外會印出診斷訊息，這代表開機路徑其實已經失敗。
# An unhandled CPU exception prints a diagnostic, which means the boot path
# actually failed even though the greeting was reached.
if "CPU exception" in out:
    print(out, file=sys.stderr)
    raise SystemExit(1)
print('PASS (keyboard %s)' % keyboard if x86 else 'PASS')
PY
done

# Apple Silicon 沒有模擬器，只驗證能建置出核心。
# There is no Apple Silicon emulator, so only the build is verified.
printf "[apple_silicon] build ... "
sh "$root/scripts/build.sh" apple_silicon >/dev/null
echo PASS

echo "All tests passed."
