#!/usr/bin/env sh
# 建置所有目標，並以 QEMU 執行可模擬目標的整合啟動測試。
# Build every target and run integration boot tests under QEMU for the ones
# that can be emulated.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
echo "ShirleyOS Integration Tests"
command -v qemu-system-aarch64 >/dev/null 2>&1 || { echo "Missing qemu-system-aarch64. Install with: brew install qemu" >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null 2>&1 || { echo "Missing qemu-system-x86_64. Install with: brew install qemu" >&2; exit 1; }

# 主機測試涵蓋與架構無關的核心元件、韌體資料格式解析、開機載入器邏輯、
# 不碰硬體的輸入解碼層，以及根檔案系統映像本身。
# The host tests cover the architecture-neutral kernel components, the firmware
# data format parsers, the boot loader logic, the hardware-independent input
# decoding layer, and the root file system image itself.
printf "[host]        test  ... "
host_build="$root/build/host"
cmake -S "$root" -B "$host_build" >/dev/null 2>&1
cmake --build "$host_build" >/dev/null 2>&1
ctest --test-dir "$host_build" --output-on-failure >/dev/null
echo PASS

# 逐一建置可模擬的目標，開機後在真正的 shell 提示符裡輸入指令。
# Build each emulatable target in turn, then type commands at the shell prompt
# the machine actually boots into.
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
import os, re, socket, subprocess, sys, tempfile, threading, time

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
prompt = 'shirley:/$'
collected = []

process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)

# QEMU 的輸出必須一邊執行一邊讀，否則輸入還沒送出去管線就已經塞住。逐字元
# 讀而不是逐行讀：提示符後面沒有換行，等整行才收下的話永遠等不到它。
#
# QEMU's output has to be drained while it runs, or the pipe fills up before
# any input is sent. It is read one character at a time rather than by lines:
# the prompt has no trailing newline, and waiting for a whole line would mean
# never seeing it.
def drain():
    while True:
        character = process.stdout.read(1)
        if not character:
            return
        collected.append(character)
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

# 一次送一個字元，模擬真正的打字：終端機本來就是一個按鍵一個中斷。
# Send one character at a time, the way real typing arrives: a terminal
# produces one interrupt per key.
def type_line(text, settle=1.0):
    baseline = len(output())
    for character in text + '\n':
        process.stdin.write(character)
        process.stdin.flush()
        time.sleep(0.03)
    deadline = time.time() + 5
    while time.time() < deadline:
        if output()[baseline:].count(prompt) >= 1:
            break
        time.sleep(0.1)
    time.sleep(settle)
    return output()[baseline:]

def expect(answer, needle, what):
    if needle not in answer:
        fail('%s: expected %r in the shell output' % (what, needle))

# 開機的終點就是 shell 提示符；提示符出現代表核心跑完了整個啟動流程，
# 並且掛上了根檔案系統。
#
# Boot ends at the shell prompt. Its appearance means the kernel completed the
# whole start-up path and mounted the root file system.
if not wait_for(prompt, timeout):
    fail('The shell prompt never appeared')

# x86 目標先用注入的實體按鍵敲一行指令：每個字元都必須回顯，而且指令要真的
# 執行，這證明 IRQ1 的 end-of-interrupt 沒有漏掉。ARM64 沒有 PS/2 鍵盤，
# 它的輸入全部走序列埠，由後面共用的那幾行指令涵蓋。
#
# On the x86 targets a line is typed with injected physical keys first: every
# character has to echo and the command has to actually run, which proves no
# end-of-interrupt was missed on IRQ1. ARM64 has no PS/2 keyboard and takes all
# of its input over the serial port, which the shared commands below cover.
keyboard = 'not attempted'
if x86:
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
        names = {' ': 'spc', '/': 'slash', '.': 'dot', '\n': 'ret'}
        for character in 'pwd\n':
            monitor.sendall(('sendkey %s\n' % names.get(character, character)).encode())
            time.sleep(0.15)
        monitor.close()
        deadline = time.time() + 5
        while time.time() < deadline and 'pwd' not in output()[baseline:]:
            time.sleep(0.1)
        answer = output()[baseline:]
        if 'pwd' not in answer:
            fail('Keys sent to the PS/2 controller were not echoed by the shell')
        if '\n/' not in answer:
            fail('The command typed on the PS/2 keyboard did not run')
        keyboard = 'PASS'

# 檔案系統：列出根目錄要看到 rootfs/ 裡真正存在的項目，讀檔要讀出檔案內容。
# The file system: listing the root has to show what rootfs/ really contains,
# and reading a file has to produce that file's contents.
answer = type_line('ls /')
expect(answer, 'README.md', 'ls /')
expect(answer, 'etc/', 'ls /')
expect(answer, 'docs/', 'ls /')

answer = type_line('cd /etc')
answer = type_line('ls')
expect(answer, 'motd', 'ls after cd')
expect(answer, 'version', 'ls after cd')

# 相對路徑要相對於工作目錄解析，提示符也要跟著工作目錄改變。
# A relative path resolves against the working directory, and the prompt
# follows the working directory.
answer = type_line('cat version')
expect(answer, 'ShirleyOS', 'cat version')
if 'shirley:/etc$' not in output():
    fail('The prompt did not follow the working directory')

answer = type_line('cd ..')
answer = type_line('pwd')
expect(answer, '\n/', 'pwd after cd ..')

# 計時器中斷必須持續送達，否則 end-of-interrupt 其實沒有生效。uptime 印出
# 的是自開機以來的計時器中斷次數，因此非零就代表 IRQ 一直在送。
#
# Timer interrupts have to keep arriving, or end-of-interrupt is not really
# working. uptime prints the timer interrupts counted since boot, so a non-zero
# count means they keep coming.
answer = type_line('uptime')
match = re.search(r'up (\d+) s \((\d+) timer interrupts at (\d+) Hz\)', answer)
if match is None:
    fail('uptime did not report the timer')
if int(match.group(2)) == 0:
    fail('The timer interrupt never arrived')

# user 程式最後才跑：它會接管 CPU，之後 shell 不會再回來。
# The user program runs last: it takes over the CPU and the shell does not come
# back afterwards.
type_line('hello', settle=2.0)
if not wait_for("Hello! Shirley's OS.", 5):
    fail('The embedded user program did not run from the shell')

process.kill()
reader.join(timeout=2)
out = output()

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
          '[IRQ] PIT timer enabled on IRQ0', '[IRQ] keyboard IRQ enabled',
          '[IRQ] serial console input enabled on IRQ4'] if x86 else \
         ['[IRQ] EL1 exception vector table initialized', '[IRQ] GICv2 initialized',
          '[IRQ] architected timer enabled on PPI 30', '[IRQ] UART console input enabled']
for stage in stages:
    if stage not in out:
        print('Missing interrupt stage: ' + stage, file=sys.stderr)
        print(out, file=sys.stderr)
        raise SystemExit(1)
# 根檔案系統必須真的掛上，否則 shell 只是個空殼。
# The root file system has to actually mount, or the shell is an empty shell.
if 'Root file system: mount failed' in out or 'Root file system:' not in out:
    print('The root file system did not mount', file=sys.stderr)
    print(out, file=sys.stderr)
    raise SystemExit(1)
# 未處理的 CPU 例外會印出診斷訊息，這代表開機路徑其實已經失敗。
# An unhandled CPU exception prints a diagnostic, which means the boot path
# actually failed even though the prompt was reached.
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
