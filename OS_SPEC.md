# ShirleyOS 規格書 / ShirleyOS Specification

本文件採中英並存，中文在前、英文在後；兩者內容必須一致，不得只更新其中一種。

This document is bilingual, Chinese first and English second. The two must
always say the same thing; never update only one of them.

## 目標 / Goals

ShirleyOS 的設計目標是能在各種機器上執行、保持穩定、易於維護，並具備高效率。

ShirleyOS is designed to run everywhere, remain stable, be easy to maintain,
and deliver high efficiency.

## CPU 架構與平台模型 / CPU architectures and platform model

目前支援的 CPU 架構是 x86_64 與 ARM64，RISC-V64 在規劃中。CPU 架構與機器平台
是兩個分開的概念：

Current CPU architectures are x86_64 and ARM64; RISC-V64 is planned. CPU
architecture and machine platform are separate concepts:

```text
arch/     x86_64/  arm64/
platform/ qemu_x86_64/  pc/  qemu_arm64/  apple_silicon/  firmware/
```

Apple Silicon 是一個 ARM64 平台，不是一種 CPU 架構。新增架構時主要應該新增
`arch/<architecture>/`；新增平台時主要應該新增 `platform/<platform>/`。因此
Apple Silicon 等於 `arch/arm64/` 加上 `platform/apple_silicon/`。

Apple Silicon is an ARM64 platform, not a CPU architecture. Adding an
architecture should primarily add `arch/<architecture>/`; adding a platform
should primarily add `platform/<platform>/`. Thus Apple Silicon is
`arch/arm64/` plus `platform/apple_silicon/`.

建置系統把這兩個維度視為獨立選項，分別由 `SHIRLEY_ARCH` 與 `SHIRLEY_PLATFORM`
指定；`SHIRLEY_TARGET` 只是常用組合的簡寫：

The build treats the two axes independently. `SHIRLEY_ARCH` and
`SHIRLEY_PLATFORM` select them; `SHIRLEY_TARGET` is only a shorthand for the
common pairs:

| `SHIRLEY_TARGET` | `SHIRLEY_ARCH` | `SHIRLEY_PLATFORM`  |
| ---------------- | -------------- | ------------------- |
| `host`           | `host`         | `host`              |
| `x86_64`         | `x86_64`       | `qemu_x86_64`       |
| `x86_64_uefi`    | `x86_64`       | `qemu_x86_64_uefi`  |
| `arm64`          | `arm64`        | `qemu_arm64`        |
| `arm64_uefi`     | `arm64`        | `qemu_arm64_uefi`   |
| `apple_silicon`  | `arm64`        | `apple_silicon`     |

同一台機器搭配不同韌體算是不同的平台。`qemu_x86_64` 與 `qemu_x86_64_uefi` 指的
是同一台 QEMU PC，只是一個由 BIOS 開機、另一個由 OVMF 開機；兩者共用
`platform/pc/` 底下所有裝置驅動程式，只有韌體交接不同。ARM64 的
`qemu_arm64` 與 `qemu_arm64_uefi` 也是同樣的關係。

The same machine with different firmware counts as a different platform.
`qemu_x86_64` and `qemu_x86_64_uefi` are the same QEMU PC, one booted by a
BIOS and the other by OVMF; they share every device driver under
`platform/pc/` and differ only in the firmware handoff. `qemu_arm64` and
`qemu_arm64_uefi` stand in the same relationship.

`platform/` 底下有兩個目錄不代表機器：

Two `platform/` directories are not machines:

* `platform/pc/` 存放所有 IBM PC 相容機器共用的硬體，例如 8259A 中斷控制器。
* `platform/firmware/` 存放**韌體資料格式**，同一種格式可能出現在多台機器上：
  BIOS E820 記憶體地圖、flattened device tree、Apple `boot_args`，以及 UEFI
  記憶體地圖。這些都是純粹的資料轉換，不碰任何硬體，因此會編進主機建置並由
  測試涵蓋。

* `platform/pc/` holds hardware shared by every IBM-PC-compatible machine,
  such as the 8259A interrupt controller and the COM1 serial console.
* `platform/firmware/` holds firmware *data formats* that more than one
  machine can present: BIOS E820 memory maps, flattened device trees, Apple
  `boot_args`, and UEFI memory maps. These are pure data transformations with
  no hardware access, so they are compiled into the host build and covered by
  tests.

### 架構抽象層 / Architecture abstraction

`include/shirley/arch.hpp` 是通用核心程式碼與 ISA 之間的完整契約：CPU 初始化
與辨識、中斷開關、閒置與停機、堆疊指標、位址空間切換、切換到使用者模式，
以及中斷處理常式註冊。

`include/shirley/arch.hpp` is the whole contract between generic kernel code
and an ISA: CPU initialization and identification, interrupt enable/disable,
idle and halt, stack pointer, address-space switching, the transition to user
mode, and interrupt-handler registration.

平台程式碼確實需要的架構專用標頭檔放在 `include/shirley/arch/<architecture>/`。
只有該架構自己的程式碼，以及為該架構建置的平台可以引用它們，通用核心程式碼
一律不得引用。目前包含 x86_64 的 I/O port 與分頁表，以及 ARM64 的例外向量
編號與轉換表。

Architecture-specific headers that platform code legitimately needs live in
`include/shirley/arch/<architecture>/`. They may only be included by that
architecture's own code and by platforms built for it — never by generic
kernel code. Today these are x86_64 port I/O and page tables, and the ARM64
exception-vector numbering and page tables.

`register_interrupt_handler()` 接受的是架構中斷向量，而向量空間的意義由各架構
自行定義：

`register_interrupt_handler()` takes an architecture interrupt vector, and
the vector space is defined per architecture:

* x86_64：IDT 向量 0-255。向量 0-31 是 CPU 例外；平台中斷控制器會重新對應，
  讓裝置 IRQ *n* 出現在向量 32+*n*。
* ARM64：`include/shirley/arch/arm64/exception.hpp` 中的 16 個 EL1 例外向量
  入口。所有裝置中斷都集中送到 IRQ 入口，再由中斷控制器驅動程式分辨來源。

* x86_64: IDT vectors 0-255. Vectors 0-31 are CPU exceptions; the platform
  interrupt controller is remapped so device IRQ *n* arrives on vector 32+*n*.
* ARM64: the 16 EL1 exception-vector entries from
  `include/shirley/arch/arm64/exception.hpp`. Device interrupts all arrive on
  an IRQ entry, and the interrupt-controller driver demultiplexes them.

兩種架構的共同規則是：沒有註冊處理常式的例外會在主控台印出暫存器內容並停住
處理器，而不是安靜地繼續執行。

Either way, an exception with no registered handler prints a register dump on
the console and stops the processor rather than continuing silently.

### 平台抽象層 / Platform abstraction

`include/shirley/platform.hpp` 涵蓋機器識別、`Capabilities` 記錄（讓通用程式碼
永遠不必用機器名稱做判斷）、中斷控制器的遮罩與 end-of-interrupt、平台 IRQ 到
架構向量的對應，以及關機與重開機。

`include/shirley/platform.hpp` covers machine identity, a `Capabilities`
record so generic code never tests for a machine by name, interrupt-controller
masking and end-of-interrupt, the platform IRQ to architecture vector mapping,
and power off/restart.

每個平台另外提供 `shirley_platform_boot_info()`，把韌體交給核心的任何資料轉換成
`include/shirley/boot_info.hpp` 中與機器無關的 `BootInfo`。

Each platform also supplies `shirley_platform_boot_info()`, which converts
whatever the firmware handed the kernel into the neutral `BootInfo` in
`include/shirley/boot_info.hpp`.

## 目前的開發環境 / Current development environment

```text
Apple Silicon Mac -> macOS -> Clang/LLVM -> QEMU -> ShirleyOS ARM64/x86_64
```

Tier 1 不需要任何實體目標硬體。Tier 1 參考平台是 QEMU ARM64 `virt` 與
QEMU x86_64。Tier 2 平台是 Apple Silicon 與標準的 x86_64 UEFI PC。QEMU 沒有
Apple Silicon 機器模型，因此 `apple_silicon` 目標只會建置與審閱，不會被開機；
要在實機上執行需要 m1n1 之類的載入器，屬於 M8。

No physical target hardware is required for Tier 1. Tier 1 reference platforms
are QEMU ARM64 `virt` and QEMU x86_64. Tier 2 platforms are Apple Silicon and
a standard x86_64 UEFI PC. QEMU has no Apple Silicon machine model, so the
`apple_silicon` target is built and reviewed but not booted; running it on
real hardware needs an m1n1-style loader and belongs to M8.

## 核心分層 / Kernel layering

```text
Applications
libc / POSIX Compatibility
ShirleyOS Native ABI
Generic Kernel
Architecture Abstraction
Platform Abstraction
Hardware
```

通用核心程式碼不得假設自己跑在 x86_64、ARM64、QEMU 或 Apple Silicon 上。
架構差異屬於 `arch/`；機器與裝置差異屬於 `platform/`。POSIX 相容是介面目標，
不是核心的內部架構。主控台的分界線是 `shirley::console::{initialize,write}`。

Generic kernel code must not assume x86_64, ARM64, QEMU, or Apple Silicon.
Architecture differences belong in `arch/`; machine and device differences
belong in `platform/`. POSIX compatibility is an interface goal, not the
internal kernel architecture. The console boundary is
`shirley::console::{initialize,write}`.

核心是 freestanding 的。`kernel/freestanding/` 提供核心實際用到的標準標頭檔
最小子集，`kernel/runtime/` 則提供編譯器會隱含產生呼叫的常式（`memset`、
`memcpy`、`memmove`、`memcmp`、`operator delete`、`__cxa_pure_virtual`）。
核心沒有堆積，因此每個 `operator delete` 都直接停住處理器，而不是假裝完成釋放。

The kernel is freestanding. `kernel/freestanding/` supplies the minimal subset
of standard headers the kernel actually uses, and `kernel/runtime/` supplies
the routines the compiler emits implicitly (`memset`, `memcpy`, `memmove`,
`memcmp`, `operator delete`, `__cxa_pure_virtual`). There is no heap, so every
`operator delete` stops the processor instead of pretending to free memory.

## 開機架構 / Boot architecture

長期的正式開機路徑是：

The long-term production path is:

```text
Firmware -> ShirleyOS Bootloader -> ShirleyOS Kernel -> /bin/init
```

### 開發用開機模式 / Development Boot Mode

里程碑 M0 允許讓 QEMU 直接載入核心，以加快開發速度。

Milestone M0 permits direct QEMU kernel loading for rapid development.

* ARM64 使用 QEMU `virt -kernel`。韌體以 `x0` 傳入 flattened device tree；
  `platform/qemu_arm64` 會讀取其 `/memory` 節點與記憶體保留區塊。
* x86_64 使用 `arch/x86_64/boot.S` 中 512 位元組的 BIOS 開機磁區。它會收集
  E820 記憶體地圖、讀入核心、以 2 MiB 分頁對前 1 GiB 建立 identity mapping、
  進入長模式，然後以 `RDI` 帶著記憶體地圖位址跳進核心。
* Apple Silicon 預期 `x0` 指向 Apple 的 `boot_args` 結構，與 iBoot 和 m1n1
  使用的契約相同。

* ARM64 uses QEMU `virt -kernel`. The firmware passes a flattened device tree
  in `x0`; `platform/qemu_arm64` reads its `/memory` node and reservation
  block.
* x86_64 uses a 512-byte BIOS boot sector in `arch/x86_64/boot.S`. It collects
  the E820 memory map, reads the kernel, identity-maps the first 1 GiB with
  2 MiB pages, enters long mode, and jumps to the kernel with the memory map
  address in `RDI`.
* Apple Silicon expects `x0` to hold an Apple `boot_args` structure, the same
  contract iBoot and m1n1 use.

這些都只是開發階段的啟動機制，不是永久的正式開機依賴。

These are development launch mechanisms, not a permanent production boot
dependency.

### UEFI 開機路徑 / The UEFI boot path

`boot/uefi/` 是 ShirleyOS 自己的 UEFI 開機載入器，也就是上面正式路徑中
「ShirleyOS Bootloader」的第一個實作。它是一個 PE32+ 的 EFI 應用程式，
在 x86_64 上由 OVMF 載入、在 ARM64 上由 EDK2/AAVMF 載入，是整個專案裡唯一
在韌體環境中執行的部分。

`boot/uefi/` is ShirleyOS's own UEFI boot loader, the first implementation of
the "ShirleyOS Bootloader" step in the production path above. It is a PE32+
EFI application, loaded by OVMF on x86_64 and by EDK2/AAVMF on ARM64, and it
is the only part of the project that runs inside the firmware environment.

它的工作依序是：從自己所在的磁碟區讀出 `\shirley\kernel.elf`、依 program
header 把每個 `PT_LOAD` 節區搬到核心要求的實體位址並清零 `.bss`、記錄 GOP
framebuffer 與 ACPI RSDP、呼叫 `GetMemoryMap` 與 `ExitBootServices`，最後把
UEFI 記憶體地圖轉成通用格式並跳進核心。

In order, it reads `\shirley\kernel.elf` from the volume it was loaded from,
copies every `PT_LOAD` segment to the physical address the kernel asks for and
zeroes the `.bss` tail, records the GOP framebuffer and the ACPI RSDP, calls
`GetMemoryMap` and `ExitBootServices`, and finally converts the UEFI memory map
into the neutral format and jumps into the kernel.

有三件事是這條路徑特有的限制，實作與審閱時都必須記得：

Three constraints are specific to this path and must be kept in mind when
implementing or reviewing it:

* `ExitBootServices` 之後不能再配置記憶體，因此交接需要的每一塊緩衝區都必須
  事先配置好。這也是 `uefi_memory_map()` 寫進呼叫端提供的陣列、而不是自行
  配置的原因。
* 走訪記憶體地圖的步幅是韌體回報的 `descriptor_size`，不是 `sizeof`。
* x86_64 的 UEFI 使用 Microsoft x64 呼叫慣例，核心卻是 System V。載入器呼叫
  核心進入點時必須明確標註 `sysv_abi`，否則交接指標會送到錯誤的暫存器。

* No memory can be allocated after `ExitBootServices`, so every buffer the
  handoff needs must exist before that call. This is why `uefi_memory_map()`
  writes into a caller-supplied array instead of allocating one.
* The memory map is walked with the firmware-reported `descriptor_size` as the
  stride, never with `sizeof`.
* UEFI on x86_64 uses the Microsoft x64 calling convention while the kernel
  uses System V. The loader must mark the kernel entry point `sysv_abi`, or the
  handoff pointer arrives in the wrong register.

開發時 QEMU 直接把一個目錄當成 FAT 磁碟區提供給韌體，因此不需要真的產生
FAT 映像；建置流程只把 `BOOTX64.EFI`／`BOOTAA64.EFI` 與核心 ELF 放進
`esp/` 目錄。

For development QEMU serves a directory to the firmware as a FAT volume, so no
real FAT image is produced; the build simply stages `BOOTX64.EFI` or
`BOOTAA64.EFI` and the kernel ELF into an `esp/` directory.

### 開機協定契約 / Boot protocol contract

每個平台都必須提供：

Every platform provides:

```c
const shirley::BootInfo* shirley_platform_boot_info(const void* firmware_table);
```

架構的進入點程式碼會遮罩中斷、清零 `.bss`、建立開機堆疊、以韌體指標呼叫這個
函式，再把結果交給 `kernel_main()`。核心映像本身、韌體資料與任何仍在使用的
韌體表格都會被回報為不可使用，分頁分配器因此永遠不會把它們配置出去。

The architecture entry code masks interrupts, zeroes `.bss`, establishes the
boot stack, calls this function with the firmware pointer, and passes the
result to `kernel_main()`. The kernel image itself, the firmware data, and any
live firmware tables are reported as non-usable so the page allocator can never
hand them out.

`firmware_table` 的內容由平台決定：BIOS 平台收到的是 E820 表格位址，裝置樹
平台收到的是 DTB 位址，Apple Silicon 收到的是 `boot_args`，而 UEFI 平台收到的
是 `include/shirley/boot_protocol.hpp` 中的 `BootHandoff`。前三者需要平台自行
解析韌體格式；UEFI 平台則因為載入器已經完成轉換，只需驗證 magic 與版本。
這個驗證不是形式：韌體交出控制權時暫存器裡可能殘留任何值，magic 讓核心能確定
自己拿到的真的是本專案載入器產生的資料。

What `firmware_table` points at is decided by the platform: a BIOS platform
receives the address of the E820 table, a device tree platform receives the
DTB, Apple Silicon receives `boot_args`, and a UEFI platform receives the
`BootHandoff` from `include/shirley/boot_protocol.hpp`. The first three parse a
firmware format themselves; a UEFI platform only validates a magic and version,
because the loader already did the conversion. That validation is not a
formality: a register can hold anything when firmware hands over control, and
the magic is how the kernel knows the data really came from this project's
loader.

## 里程碑 / Milestones

### M0 — QEMU 開機與主控台 / QEMU boot and console

兩個 Tier 1 目標都已完成。`./shirley` 會建置、啟動 QEMU、接上客體序列主控台，
並顯示由客體核心產生的輸出。包裝腳本絕對不可以偽造客體輸出。

Complete for both Tier 1 targets. `./shirley` builds, launches QEMU, attaches
the guest serial console, and shows output produced by the guest kernel. The
wrapper must never fake guest output.

### M0.5 — 架構與平台啟用 / architecture and platform bring-up

目前的里程碑。兩個架構都會安裝真正的 CPU 狀態：x86_64 建立自己的 GDT、TSS
與 256 項 IDT 並附帶例外診斷輸出；ARM64 安裝 EL1 例外向量表。兩者都提供
`memory::AddressSpace` 的分頁表實作、進入使用者模式的路徑，以及 CPU 辨識。
每個平台都提供開機協定與真實的記憶體地圖，因此分頁分配器跑在真正的韌體資料
上，而不是寫死的區段。

Current milestone. Both architectures install real CPU state: x86_64 sets up
its own GDT, TSS, and a 256-entry IDT with exception reporting; ARM64 installs
the EL1 exception vector table. Both provide page-table implementations of
`memory::AddressSpace`, a user-mode entry path, and CPU identification. Every
platform supplies a boot protocol and a real memory map, so the page allocator
runs on genuine firmware data rather than a hardcoded region.

本里程碑也加入了 UEFI 開機路徑：`boot/uefi/` 是 ShirleyOS 自己的 UEFI 載入器，
`qemu_x86_64_uefi` 與 `qemu_arm64_uefi` 則是對應的平台。這是正式開機架構中
「ShirleyOS Bootloader」的第一個實作，因此 x86_64 不再只能靠開發用的 BIOS
開機磁區。

This milestone also brings up the UEFI boot path: `boot/uefi/` is ShirleyOS's
own UEFI loader and `qemu_x86_64_uefi` and `qemu_arm64_uefi` are the matching
platforms. This is the first implementation of the "ShirleyOS Bootloader" step
of the production boot architecture, so x86_64 no longer depends solely on the
development BIOS boot sector.

本里程碑尚未完成的部分：ARM64 的 MMU 已經實作（`arch::arm64::mmu_enable`）
但開機時尚未啟用；也還沒有計時器或中斷控制器的分派驅動程式，因此所有裝置
IRQ 都維持遮罩狀態。UEFI 載入器面對韌體的那一段只經過審閱，尚未實際開機驗證；
可以在沒有韌體的情況下測試的部分（ELF 讀取、記憶體地圖轉換、交接驗證）
則已由 `tests/boot_loader_smoke.cpp` 涵蓋。

Not yet done in this milestone: the ARM64 MMU is implemented
(`arch::arm64::mmu_enable`) but not enabled at boot, and no timer or
interrupt-controller demultiplexing driver exists yet, so all device IRQs stay
masked. The firmware-facing part of the UEFI loader has been reviewed but not
yet booted; the parts that can be tested without firmware — ELF reading, memory
map conversion, handoff validation — are covered by
`tests/boot_loader_smoke.cpp`.

### M1 — 記憶體與使用者空間 / memory and userspace

實作開機載入器交接、在兩個架構上啟用虛擬記憶體、堆積、ELF 載入器、使用者
空間、最小 libc、C 的 `main()`、`printf()` 與系統呼叫。共用的 `kernel/`、
`libc/` 與 `user/hello/main.c` 原始碼必須同時支援目前兩個架構；ISA 相關的
工作留在 `arch/` 底下。hello 這支 C 程式必須真的在使用者空間執行。

Implement bootloader handoff, virtual memory activation on both
architectures, heap, ELF loader, userspace, minimal libc, C `main()`,
`printf()`, and syscalls. The shared `kernel/`, `libc/`, and
`user/hello/main.c` sources must support both current architectures;
ISA-specific work stays under `arch/`. The hello C program must actually
execute in userspace.

### 後續規劃 / Roadmap

M2 中斷、計時器、排程器與執行緒；M3 行程與 IPC；M4 VFS、initramfs 與 shell；
M5 PCI/VirtIO 與儲存；M6 網路；M7 實體 x86_64 PC 啟用；M8 Apple Silicon 啟用；
M9 GUI 與視窗系統。

M2 interrupts, timer, scheduler, and threads; M3 processes and IPC; M4 VFS,
initramfs, and shell; M5 PCI/VirtIO and storage; M6 networking; M7 physical
x86_64 PC bring-up; M8 Apple Silicon bring-up; M9 GUI/window system.

## 程式撰寫理念 / Coding philosophy

邊界要可移植，核心要簡單，熱路徑要有效率。先做出簡單且可測試的實作，再談
最佳化。通用核心程式碼不應該在意自己跑在 x86_64、ARM64、QEMU、PC 或
Apple Silicon 上。

Portable at the boundaries, simple in the core, efficient on the hot path.
Prefer simple, testable implementations before optimization. Generic kernel
code should not care whether it runs on x86_64, ARM64, QEMU, PC, or Apple
Silicon.

## 註解語言 / Comment language

所有程式碼註解採中英並存，中文在前、英文在後。多行區塊以空的註解行分隔兩種
語言，單行說明則以「中文 / English」的形式寫在同一行。純識別字（例如
`} // namespace`、暫存器名稱、C 函式原型）不需要翻譯。

Every code comment is bilingual, Chinese first and English second. A
multi-line block separates the two languages with a blank comment line, while
a short note uses the form "中文 / English" on one line. Pure identifiers such
as `} // namespace`, register names, and C prototypes need no translation.

`.S` 檔案會經過 C 前處理器，因此註解行不可以由 `# error`、`# define` 之類的
前處理器關鍵字開頭。

`.S` files pass through the C preprocessor, so no comment line may begin with
a preprocessor keyword such as `# error` or `# define`.

## 規格維護 / Specification maintenance

只要變更架構、ABI、開機協定、記憶體模型、平台抽象、系統呼叫、驅動程式模型、
支援的目標或里程碑狀態，就必須在同一次變更中更新本規格書，而且中英兩種版本
都要更新。實作與規格不得在無人察覺的情況下分歧。

Changes to architecture, ABI, boot protocol, memory model, platform
abstraction, syscalls, driver model, supported targets, or milestone status
must update this specification in the same change, in both languages.
Implementation and specification must not silently diverge.
