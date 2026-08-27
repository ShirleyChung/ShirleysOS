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

## Current status

里程碑 M0.5 讓兩個架構真正啟動：x86_64 會自行安裝 GDT、TSS 和 IDT，並提供 CPU 例外回報，同時透過它驅動真實裝置中斷；ARM64 則安裝 EL1 例外向量表。兩個架構都提供通用位址空間介面的分頁表實作與使用者模式進入路徑。每個平台都會將其韌體的記憶體地圖（BIOS E820、flattened device tree、Apple `boot_args` 或 UEFI 記憶體地圖）轉換成中立的 `BootInfo`，由它驅動頁面分配器。

Milestone M0.5 brings up both architectures for real: x86_64 installs its own GDT, TSS, and IDT with CPU exception reporting, and drives real device interrupts through it; ARM64 installs the EL1 exception vector table. Both architectures provide a page-table implementation of the generic address-space interface and a user-mode entry path. Every platform converts its firmware memory map — BIOS E820, a flattened device tree, Apple `boot_args`, or a UEFI memory map — into the neutral `BootInfo` that drives the page allocator.

x86_64 也擁有完整可用的中斷子系統：256 個入口的 IDT、作為啟動中斷控制器後端的 8259A、供裝置驅動程式使用的通用 `shirley::irq` 層、IRQ0 上的 100 Hz PIT，以及 IRQ1 上的中斷驅動 PS/2 鍵盤；鍵盤輸入會被回顯到 console 並排入標準輸入。核心在空閒時使用 `hlt`，不進行輪詢。

x86_64 also has a working interrupt subsystem end to end: a 256-entry IDT, the 8259A as its bring-up interrupt controller backend, a generic `shirley::irq` layer that device drivers use instead of touching a controller, a 100 Hz PIT on IRQ0, and an interrupt-driven PS/2 keyboard on IRQ1 whose characters are echoed to the console and queued as standard input. The kernel idles in `hlt` and polls nothing.

ARM64 也在同一個 `shirley::irq` 介面下實作相同的中斷子系統。它的控制器採用多工分派，而不是為每個 IRQ 分配獨立向量：每個裝置中斷都會進入同一個 IRQ 例外入口，控制器驅動程式再辨識來源並進行派送。`platform/arm/` 儲存的是 ARM 定義的內容，而不是任何單一機器的實作；其中包含 GICv2 驅動與 PPI 30 上的架構定時器，與 `platform/pc/` 相對應。`qemu_arm64` 和 `qemu_arm64_uefi` 也都會透過它運作 100 Hz 計時器。

ARM64 now has the same subsystem behind the same `shirley::irq` interface. Its controllers demultiplex rather than giving each IRQ its own vector: every device interrupt arrives on the one IRQ exception entry, and the controller driver identifies the source and dispatches it. `platform/arm/` holds what ARM defines rather than any one machine — a GICv2 driver and the architected timer on PPI 30, the ARM counterpart of `platform/pc/` — and `qemu_arm64` and `qemu_arm64_uefi` both run a 100 Hz timer through it.

Apple Silicon 改為使用 `platform/apple_silicon/` 中自己的 AIC，現在已成為完整路徑，而不只是暫存器存取層面。它仍未真正運行：QEMU 沒有 Apple Silicon 機器模型，因此該目標僅完成建置與檢視，尚未啟動；其暫存器配置來自 Asahi Linux 公開文件，而非原廠資料表。

Apple Silicon uses its own AIC in `platform/apple_silicon/` instead, which is now a complete path rather than just register access. It has still never been executed: QEMU has no Apple Silicon machine model, so that target is built and reviewed but not booted, and its register layout comes from Asahi Linux's published documentation rather than a datasheet.

如需了解架構的真實來源與未來路線圖，請參閱 [OS_SPEC.md](OS_SPEC.md)。See [OS_SPEC.md](OS_SPEC.md) for the architectural source of truth and roadmap.

在 QEMU 下啟動 x86_64 時，會從 guest kernel 自己的序列埠輸出以下訊息：

```text
[IRQ] IDT initialized
[IRQ] PIC remapped 0x20/0x28
[IRQ] PIT timer enabled on IRQ0
[IRQ] keyboard IRQ enabled
ShirleyOS booting...
Architecture: x86_64
Processor: GenuineIntel
Platform: QEMU x86_64
Machine: QEMU PC with SeaBIOS firmware
Memory regions: 7
Usable memory: 511 MiB
Free pages: 130870
Interrupts: enabled
Timer: 100 Hz
Keyboard: type to echo through the interrupt path
Hello! Shirley's OS.
[IRQ] timer ticking: 100 interrupts in the first second
```

Booting x86_64 under QEMU prints the following from the guest kernel's own serial port:

```text
[IRQ] IDT initialized
[IRQ] PIC remapped 0x20/0x28
[IRQ] PIT timer enabled on IRQ0
[IRQ] keyboard IRQ enabled
ShirleyOS booting...
Architecture: x86_64
Processor: GenuineIntel
Platform: QEMU x86_64
Machine: QEMU PC with SeaBIOS firmware
Memory regions: 7
Usable memory: 511 MiB
Free pages: 130870
Interrupts: enabled
Timer: 100 Hz
Keyboard: type to echo through the interrupt path
Hello! Shirley's OS.
[IRQ] timer ticking: 100 interrupts in the first second
```

記憶體數字會隨著 kernel 映像檔變大而變動。在 QEMU 顯示視窗中輸入字元，會透過 IRQ1 路徑回顯；QEMU 會從顯示裝置讀取鍵盤事件，因此 `./shirley x86_64` 預設會開啟一個顯示視窗。若要使用舊的 `-nographic` 行為，可設定 `SHIRLEY_HEADLESS=1`，這會變成純輸出模式，沒有鍵盤輸入。

The memory figures move as the kernel image grows. Typing in the QEMU display window echoes characters through the IRQ1 path; QEMU sources keyboard events from its display device, so `./shirley x86_64` opens one by default. Set `SHIRLEY_HEADLESS=1` for the old `-nographic` behaviour, which is output-only and has no keyboard.

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
