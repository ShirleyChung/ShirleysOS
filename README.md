# ShirleyOS

ShirleyOS 不是研究型作業系統；它是為了部署到各種機器上並支援應用程式開發而設計的系統。它以「CPU 架構」與「硬體平台」嚴格分離為核心，讓系統能在不同機器上保持穩定、易於維護，並具備高效率。

ShirleyOS is not a research OS; it is designed to be deployed across many machines and to support application development. It is built around a strict separation between CPU architecture and hardware platform so the system can remain stable, easy to maintain, and efficient on different machines.

目前的主要目標是 QEMU x86_64 和 QEMU ARM64；Apple Silicon 會以 ARM64 架構來處理，並使用獨立的 `platform/apple_silicon` 實作。

The first targets are QEMU x86_64 and QEMU ARM64. Apple Silicon is treated as an ARM64 architecture and implemented through a separate `platform/apple_silicon` path.

## Layout

* `kernel/` 是核心程式碼目錄，包含與架構和平台無關的核心程式碼；`kernel/` contains architecture- and platform-neutral kernel code.
* `arch/` 包含 ISA（指令集架構）與特權等級相關程式碼；`arch/` contains ISA and privilege-level code.
* `platform/` 包含機器、韌體與裝置整合相關程式碼；`platform/` contains machine, firmware, and device integration code.
* `platform/pc/` 是所有 PC 相容機器共用的硬體抽象，`platform/firmware/` 是跨機器共用的韌體資料格式；`platform/pc/` is the shared hardware abstraction for PC-compatible machines, and `platform/firmware/` holds firmware data formats shared across machines.
* `boot/` 包含啟動載入器；`boot/` contains the boot loaders.
* `boot/uefi/` 是 ShirleyOS 的 UEFI 啟動載入器：它是一個 PE32+ EFI 應用程式，可在 x86_64 上由 OVMF 執行，在 ARM64 上由 EDK2/AAVMF 執行，會載入 kernel ELF，退出 boot services，並交接已驗證的 `BootHandoff`；`boot/uefi/` is the ShirleyOS UEFI boot loader: a PE32+ EFI application that runs under OVMF on x86_64 and EDK2/AAVMF on ARM64, loads the kernel ELF, exits boot services, and hands over a validated `BootHandoff`.
* `boot/common/` 是所有載入器共用的程式碼；`boot/common/` is the shared code used by all boot loaders.
* `libc/` 是共用函式庫，但 syscall trampoline 位於 `libc/arch/`；`libc/` is the shared library layer, with syscall trampolines in `libc/arch/`.
* `rootfs/` 是根檔案系統的內容，建置時會被打包成唯讀映像連進核心；`rootfs/` is the content of the root file system, packed into a read-only image and linked into the kernel at build time.

## Current status

里程碑 M0.5 讓兩個架構真正啟動：x86_64 會自行安裝 GDT、TSS 和 IDT，並提供 CPU 例外回報，同時透過它驅動真實裝置中斷；ARM64 則安裝 EL1 例外向量表。兩個架構都提供通用位址空間介面的分頁表實作與使用者模式進入路徑。每個平台都會將其韌體的記憶體地圖（BIOS E820、flattened device tree、Apple `boot_args` 或 UEFI 記憶體地圖）轉換成中立的 `BootInfo`，由它驅動頁面分配器。

Milestone M0.5 brings up both architectures for real: x86_64 installs its own GDT, TSS, and IDT with CPU exception reporting, and drives real device interrupts through it; ARM64 installs the EL1 exception vector table. Both architectures provide a page-table implementation of the generic address-space interface and a user-mode entry path. Every platform converts its firmware memory map — BIOS E820, a flattened device tree, Apple `boot_args`, or a UEFI memory map — into the neutral `BootInfo` that drives the page allocator.

x86_64 也擁有完整可用的中斷子系統：256 個入口的 IDT、作為啟動中斷控制器後端的 8259A、供裝置驅動程式使用的通用 `shirley::irq` 層、IRQ0 上的 100 Hz PIT，以及 IRQ1 上的中斷驅動 PS/2 鍵盤與 IRQ4 上的序列埠接收路徑；兩者解出的字元都排入同一個共用佇列成為標準輸入，回顯則交給 shell 的行編輯器。核心在空閒時使用 `hlt`，不進行輪詢。

x86_64 also has a working interrupt subsystem end to end: a 256-entry IDT, the 8259A as its bring-up interrupt controller backend, a generic `shirley::irq` layer that device drivers use instead of touching a controller, a 100 Hz PIT on IRQ0, an interrupt-driven PS/2 keyboard on IRQ1, and the serial receive path on IRQ4. Each pushes its decoded characters into its own ring buffer, which the console reads through that device, and echo is left to the shell's line editor. The kernel idles in `hlt` and polls nothing.

ARM64 也在同一個 `shirley::irq` 介面下實作相同的中斷子系統。它的控制器採用多工分派，而不是為每個 IRQ 分配獨立向量：每個裝置中斷都會進入同一個 IRQ 例外入口，控制器驅動程式再辨識來源並進行派送。`platform/arm/` 儲存的是 ARM 定義的內容，而不是任何單一機器的實作；其中包含 GICv2 驅動與 PPI 30 上的架構定時器，與 `platform/pc/` 相對應。`qemu_arm64` 和 `qemu_arm64_uefi` 也都會透過它運作 100 Hz 計時器。

ARM64 now has the same subsystem behind the same `shirley::irq` interface. Its controllers demultiplex rather than giving each IRQ its own vector: every device interrupt arrives on the one IRQ exception entry, and the controller driver identifies the source and dispatches it. `platform/arm/` holds what ARM defines rather than any one machine — a GICv2 driver and the architected timer on PPI 30, the ARM counterpart of `platform/pc/` — and `qemu_arm64` and `qemu_arm64_uefi` both run a 100 Hz timer through it.

Apple Silicon 改為使用 `platform/apple_silicon/` 中自己的 AIC，現在已成為完整路徑，而不只是暫存器存取層面。它仍未真正運行：QEMU 沒有 Apple Silicon 機器模型，因此該目標僅完成建置與檢視，尚未啟動；其暫存器配置來自 Asahi Linux 公開文件，而非原廠資料表。

Apple Silicon uses its own AIC in `platform/apple_silicon/` instead, which is now a complete path rather than just register access. It has still never been executed: QEMU has no Apple Silicon machine model, so that target is built and reviewed but not booted, and its register layout comes from Asahi Linux's published documentation rather than a datasheet.

開機的終點現在是一個可用的主控台。核心會掛上根檔案系統，然後進入 shell：輸入的每個字元都由中斷送達，`ls`、`cat`、`cd` 讀的是真正掛載起來的檔案系統，沒有輸入時 CPU 停在等待中斷的低功耗狀態，不做任何輪詢。x86_64 有兩個輸入裝置（IRQ1 的 PS/2 鍵盤與 IRQ4 的序列埠），ARM64 則靠 PL011 的接收中斷；兩者都接在主控台上，shell 不需要知道字元來自哪裡。

Boot now ends at a usable console. The kernel mounts the root file system and enters a shell: every character it reads arrived through an interrupt, `ls`, `cat`, and `cd` walk a file system that is really mounted, and with nothing to read the CPU parks in a low-power wait rather than polling anything. x86_64 has two input devices — the PS/2 keyboard on IRQ1 and the serial port on IRQ4 — while ARM64 uses the PL011's receive interrupt; both attach to the console, so the shell never learns where a character came from.

驅動程式與使用者之間有一層統一的裝置抽象：`Hardware → Driver → device_t → 註冊表 → console → VFS`。每個驅動程式維護自己的環狀緩衝區，把它包成一個具名裝置登記到 `shirley::device`，中斷處理常式只負責把位元組放進緩衝區。`device::find("kbd0")` 就能直接讀鍵盤，`devices` 這個 shell 指令會列出註冊表。目前有 `console`、`null`、`uart0`、`ram0` 與 PC 上的 `kbd0`。

A single device abstraction sits between drivers and their users: `Hardware → Driver → device_t → registry → console → VFS`. Each driver keeps its own ring buffer, publishes it as a named device in `shirley::device`, and its interrupt handler does nothing but put bytes into that buffer. `device::find("kbd0")` reads the keyboard directly, and the `devices` shell command lists the registry. Today it holds `console`, `null`, `uart0`, `ram0`, and `kbd0` on a PC.

路徑現在是核心裡唯一的名字。`shirley::vfs` 提供 `open`／`read`／`write`／`seek`／`close`，根檔案系統掛在 `/`，devfs 掛在 `/dev`，兩者在同一個名字空間底下——`cat /etc/motd` 與 `cat /dev/kbd0` 走的是同一條路。`/dev` 是註冊表之上的一層命名空間，不是另一套驅動程式：一個 VFS 節點就只是持有一個 `device::Device*`。區塊裝置多一條 `block_read`／`block_write` 直接指定磁區，因此 `blk /dev/ram0 0` 印出來的就是根檔案系統掛載時檢查的那份 SHRFS1 標頭。

A path is now the one kind of name inside the kernel. `shirley::vfs` supplies `open`, `read`, `write`, `seek`, and `close`; the root file system is mounted at `/` and devfs at `/dev`, both in one namespace — `cat /etc/motd` and `cat /dev/kbd0` take the same path. `/dev` is a namespace over the registry rather than a second set of drivers: a VFS node holds nothing but a `device::Device*`. A block device adds `block_read` and `block_write` that name sectors directly, which is why `blk /dev/ram0 0` prints the very SHRFS1 header the root file system checked when it mounted.

根檔案系統是一份唯讀的 SHRFS1 映像：`rootfs/` 在建置時被打包成位元組陣列連進核心，開機時透過 RAM disk 掛載，因此在還沒有磁碟驅動程式之前就有檔案可讀。檔案系統本身只透過 `io::BlockDevice` 存取資料，換成真正的磁碟時同一份程式碼可以直接沿用。編出來的 user 程式也一起被打包成 `/bin/hello`，因此 `exec /bin/hello` 是完整的一條路：VFS 解析路徑、檔案系統讀出內容、ELF loader 映射頁面並執行它。核心不再連結任何一份 user 映像。

The root file system is a read-only SHRFS1 image: `rootfs/` is packed into a byte array at build time, linked into the kernel, and mounted through a RAM disk at boot, so there are files to read before any disk driver exists. The file system reaches its data only through `io::BlockDevice`, so the same code carries over unchanged to a real disk. The user program the build links is packed in as `/bin/hello`, which makes `exec /bin/hello` a complete path: the VFS resolves it, the file system reads it, and the ELF loader maps its pages and runs it. No user image is linked into the kernel any more.

如需了解架構的真實來源與未來路線圖，請參閱 [OS_SPEC.md](OS_SPEC.md)。See [OS_SPEC.md](OS_SPEC.md) for the architectural source of truth and roadmap.

在 QEMU 下啟動 x86_64 時，會從 guest kernel 自己的序列埠輸出以下訊息：

```text
[IRQ] IDT initialized
[IRQ] PIC remapped 0x20/0x28
[IRQ] PIT timer enabled on IRQ0
[IRQ] keyboard IRQ enabled
[IRQ] serial console input enabled on IRQ4
ShirleyOS booting...
Architecture: x86_64
Processor: GenuineIntel
Platform: QEMU x86_64
Machine: QEMU PC with SeaBIOS firmware
Memory regions: 8
Usable memory: 511 MiB
Free pages: 130844
Interrupts: enabled
Timer: 100 Hz
Root file system: 13 entries, 13316 bytes
Devices: 5
[device] console type=char
[device] null type=char
[device] uart0 type=char
[device] kbd0 type=input
[device] ram0 type=block

Welcome to ShirleyOS.

The prompt below is a real shell: it reads keystrokes that arrived through an
interrupt, and every path it prints comes from a mounted file system.

Try:
    ls /            list the root directory
    ls /docs        list one directory
    cat /etc/motd   print this file again
    help            every command the shell knows

shirley:/$ ls
     706  README.md
   <dir>  docs/
   <dir>  etc/
   <dir>  home/
   <dir>  bin/
   <dir>  dev/
6 entries
shirley:/$ cat /etc/version
ShirleyOS 0.5 "console"
shirley:/$ blk /dev/ram0 0
0000  53 48 52 46 53 31 00 00 01 00 00 00 0d 00 00 00  SHRFS1..........
0010  20 00 00 00 00 06 00 00 00 38 00 00 00 00 00 00   ........8......
shirley:/$ exec /bin/hello
Running /bin/hello. It takes over the CPU:
the shell does not come back until the machine restarts.
Hello! Shirley's OS.
```

Booting x86_64 under QEMU prints the following from the guest kernel's own serial port:

```text
[IRQ] IDT initialized
[IRQ] PIC remapped 0x20/0x28
[IRQ] PIT timer enabled on IRQ0
[IRQ] keyboard IRQ enabled
[IRQ] serial console input enabled on IRQ4
ShirleyOS booting...
Architecture: x86_64
Processor: GenuineIntel
Platform: QEMU x86_64
Machine: QEMU PC with SeaBIOS firmware
Memory regions: 8
Usable memory: 511 MiB
Free pages: 130844
Interrupts: enabled
Timer: 100 Hz
Root file system: 13 entries, 13316 bytes
Devices: 5
[device] console type=char
[device] null type=char
[device] uart0 type=char
[device] kbd0 type=input
[device] ram0 type=block

Welcome to ShirleyOS.

The prompt below is a real shell: it reads keystrokes that arrived through an
interrupt, and every path it prints comes from a mounted file system.

Try:
    ls /            list the root directory
    ls /docs        list one directory
    cat /etc/motd   print this file again
    help            every command the shell knows

shirley:/$ ls
     706  README.md
   <dir>  docs/
   <dir>  etc/
   <dir>  home/
   <dir>  bin/
   <dir>  dev/
6 entries
shirley:/$ cat /etc/version
ShirleyOS 0.5 "console"
shirley:/$ blk /dev/ram0 0
0000  53 48 52 46 53 31 00 00 01 00 00 00 0d 00 00 00  SHRFS1..........
0010  20 00 00 00 00 06 00 00 00 38 00 00 00 00 00 00   ........8......
shirley:/$ exec /bin/hello
Running /bin/hello. It takes over the CPU:
the shell does not come back until the machine restarts.
Hello! Shirley's OS.
```

記憶體數字會隨著 kernel 映像檔變大而變動。提示符出現之後就可以直接在這個終端機裡打字：按鍵沿著序列埠進來，由 IRQ4 送到 shell。QEMU 的 PS/2 按鍵事件來自顯示裝置，因此要走 IRQ1 那條路徑時，設定 `SHIRLEY_DISPLAY=1` 開一個顯示視窗，在視窗裡打字。

The memory figures move as the kernel image grows. Once the prompt appears, type straight into this terminal: the keys arrive over the serial port and reach the shell on IRQ4. QEMU sources PS/2 key events from its display device, so set `SHIRLEY_DISPLAY=1` to open a display window and type there when it is the IRQ1 path being exercised.

## Running ShirleyOS on macOS

在 macOS 上執行 ShirleyOS 時，必要時請明確安裝主機端工具：

```sh
brew install cmake ninja llvm lld qemu
./scripts/check-deps.sh
```

Install the host tools explicitly when needed:

```sh
brew install cmake ninja llvm lld qemu
./scripts/check-deps.sh
```

在 Apple Silicon 上，預設目標是 ARM64。可以直接執行：

```sh
./shirley
./shirley arm64
./shirley x86_64
./shirley x86_64_uefi
./shirley arm64_uefi
./shirley test
./shirley debug arm64
./shirley build apple_silicon
```

On Apple Silicon, the default target is ARM64. Run it directly with:

```sh
./shirley
./shirley arm64
./shirley x86_64
./shirley x86_64_uefi
./shirley arm64_uefi
./shirley test
./shirley debug arm64
./shirley build apple_silicon
```

UEFI 目標需要韌體映像檔。QEMU 內建了一份，`scripts/find-uefi-firmware.sh` 會自動定位；如需覆蓋，可設定 `SHIRLEY_UEFI_FIRMWARE`。

The UEFI targets need a firmware image. QEMU ships with one, and `scripts/find-uefi-firmware.sh` locates it automatically; set `SHIRLEY_UEFI_FIRMWARE` to override it.

QEMU 會模擬/虛擬化客戶端機器。終端中顯示的 ShirleyOS 啟動訊息，是由客戶端核心透過 UART 寫入，而不是由 shell 包裝器寫出的。當使用 `-nographic` 時，正常退出方式是按 Ctrl-A 再按 X。除錯腳本會暫停 QEMU，並開啟一個 GDB 相容伺服器，位於 `localhost:1234`；LLDB 可透過 `gdb-remote localhost:1234` 連線。

QEMU emulates/virtualizes the guest machine. The ShirleyOS boot messages shown in the terminal are written by the guest kernel through its UART, not by the shell wrapper. Normal QEMU exit is Ctrl-A then X when using `-nographic`. The debug scripts pause QEMU and expose a GDB-compatible server on `localhost:1234`; LLDB can connect with `gdb-remote localhost:1234`.

QEMU 沒有 Apple Silicon 機器模型，因此 `apple_silicon` 目前僅是建置目標。若要在實體硬體上執行，需要 m1n1 類型的載入器，這屬於里程碑 M8。

QEMU has no Apple Silicon machine model, so `apple_silicon` is currently a build-only target. Running it on real hardware needs an m1n1-style loader and is milestone M8.

目前直接使用的 QEMU 載入器屬於 Development Boot Mode。正式生產架構仍然是韌體、ShirleyOS 啟動載入器、核心與 `/bin/init`。

The current direct QEMU loaders are in Development Boot Mode. The production architecture remains firmware, ShirleyOS bootloader, kernel, and `/bin/init`.

## Creating a Bootable USB Installer / 建立可開機 USB 安裝程式

ShirleyOS 提供工具可建立 USB 可開機安裝程式，支援在實體硬體上開機。

ShirleyOS provides tools to create a bootable USB installer for booting on physical hardware.

### Quick Start / 快速開始

```bash
# 建立 x86_64 USB 映像 / Create x86_64 USB image
./tools/make-usb-installer.sh --arch x86_64

# 建立 ARM64 USB 映像 / Create ARM64 USB image
./tools/make-usb-installer.sh --arch arm64
```

映像檔將建立在 `usb-installer/shirleyos-{arch}-installer.img`

The image will be created at `usb-installer/shirleyos-{arch}-installer.img`

### Write to USB / 寫入 USB

#### macOS
```bash
diskutil list                    # 找出 USB 裝置 / Find USB device
diskutil unmountDisk /dev/diskN  # 卸載 / Unmount
sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/rdiskN bs=1m
diskutil eject /dev/diskN        # 退出 / Eject
```

#### Linux
```bash
lsblk                            # 找出 USB 裝置 / Find USB device
sudo umount /dev/sdX*            # 卸載分割區 / Unmount partitions
sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/sdX bs=4M status=progress && sync
sudo eject /dev/sdX              # 退出 / Eject
```

### Boot from USB / 從 USB 開機

1. 將 USB 插入電腦 / Insert USB into computer
2. 開機時按 F2, F12, DEL 或 ESC / Press F2, F12, DEL, or ESC during boot
3. 選擇 USB 裝置開機 / Select USB device to boot
4. 在 BIOS/UEFI 中確認：/ Confirm in BIOS/UEFI:
   - 使用 UEFI 模式（非 Legacy）/ Use UEFI mode (not Legacy)
   - 停用 Secure Boot / Disable Secure Boot

詳細說明請參考：[docs/USB_INSTALLER.md](docs/USB_INSTALLER.md)

For detailed instructions, see: [docs/USB_INSTALLER.md](docs/USB_INSTALLER.md)

## Build

預設目標是 host build，它會編譯與架構中立的核心元件與韌體記憶體地圖解析器，讓它們能在開發機器上進行測試：

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The default target is the host build, which compiles the architecture-neutral kernel components and the firmware memory-map parsers so they can be tested on the development machine:

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

當對應的 freestanding toolchain 可用時，就會開放交叉編譯目標。架構與平台是獨立選項；`SHIRLEY_TARGET` 是常見組合的簡寫：

```sh
cmake -S . -B build/x86_64 -DSHIRLEY_TARGET=x86_64   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64.cmake
cmake -S . -B build/arm64 -DSHIRLEY_TARGET=arm64   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
cmake -S . -B build/apple -DSHIRLEY_ARCH=arm64 -DSHIRLEY_PLATFORM=apple_silicon   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
```

Cross-compilation targets are exposed when the corresponding freestanding toolchain is available. Architecture and platform are independent options; `SHIRLEY_TARGET` is a shorthand for common pairs:

```sh
cmake -S . -B build/x86_64 -DSHIRLEY_TARGET=x86_64   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64.cmake
cmake -S . -B build/arm64 -DSHIRLEY_TARGET=arm64   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
cmake -S . -B build/apple -DSHIRLEY_ARCH=arm64 -DSHIRLEY_PLATFORM=apple_silicon   -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm64.cmake
```
