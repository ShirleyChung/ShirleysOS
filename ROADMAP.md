# ShirleyOS Roadmap

這份文件說明 ShirleyOS source tree 裡每個資料夾與主要程式檔的作用，以及它們在 OS 架構中的位置。更細的設計規則以 `OS_SPEC.md` 和各目錄的 `README.md` 為準；這裡的目標是讓新讀者能從 repo layout 看懂系統分層。

## 整體架構

ShirleyOS 的核心邊界是「CPU 架構」和「硬體平台」分離：

```text
firmware / boot loader
        ↓
BootInfo / BootHandoff
        ↓
arch/<isa> + platform/<machine>
        ↓
generic kernel services
        ↓
device registry + VFS + shell + user programs
```

主要分層如下：

| 路徑 | OS 架構地位 |
| --- | --- |
| `arch/` | CPU ISA、特權層、例外/中斷向量、分頁表、user mode 進出。不可放機器裝置。 |
| `platform/` | 具體機器、韌體交接、機器共用硬體、電源控制、平台 IRQ routing。 |
| `boot/` | 韌體環境中的載入器與載入器共用邏輯，負責把 kernel ELF 載入並交出 `BootInfo`。 |
| `kernel/` | 與 CPU/平台無關的核心服務：console、device、VFS、memory、process、syscall、shell。 |
| `drivers/` | 可跨平台重用的 driver contract 或硬體無關 driver 邏輯。硬體 register 操作留在 `platform/`。 |
| `include/shirley/` | 各層之間的公開 ABI/API contract。 |
| `libc/` | user space C library 與 syscall trampoline。 |
| `user/` | 被打包進 rootfs 的 user-space ELF 程式。 |
| `rootfs/` | 開機時掛載成 `/` 的唯讀 SHRFS1 內容。 |
| `cmake/`, `scripts/`, `shirley` | 建置、打包、執行、除錯、測試入口。 |
| `tests/` | host smoke tests 與可模擬目標的整合測試依據。 |
| `docs/` | 架構邊界與設計說明。 |

## 啟動流程

1. BIOS、UEFI、QEMU `-kernel` 或未來 Apple loader 把控制權交給 ShirleyOS。
2. `boot/` 或平台 boot protocol 把韌體資料轉成通用 `BootInfo`。
3. `arch/<isa>/entry.S` 設定最早期堆疊、清 `.bss`，呼叫平台 boot protocol，再進 `kernel_main()`。
4. `kernel/kernel_main.cpp` 初始化 architecture、device registry、console、memory、platform drivers、rootfs、VFS、syscall/process，最後執行 shell 或 user init。
5. `rootfs/` 被建置成 SHRFS1 byte array，核心透過 RAM disk 掛載成 `/`，`devfs` 掛載成 `/dev`。
6. shell 和 user ELF 都透過 VFS 路徑讀檔；user 程式透過 `libc` syscall wrapper 回到 kernel。

## 根目錄檔案

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | 專案總覽、目前狀態、執行方式。 | 對外入口文件。 |
| `OS_SPEC.md` | 中英雙語規格書，定義長期架構與 milestone。 | 架構 source of truth。 |
| `CMakeLists.txt` | 定義 target、arch/platform 組合、kernel/user/libc/boot build graph。 | 建置系統核心。 |
| `shirley` | 單一 CLI 入口：`run`、`build`、`debug`、`test`。 | 開發者操作層。 |
| `ROADMAP.md` | 本文件。 | source tree 導覽。 |

## `arch/`

`arch/` 只描述 CPU ISA 與特權機制。通用核心透過 `include/shirley/arch.hpp` 呼叫它，不直接碰 GDT、IDT、CR3、TTBR、EL1 vector 等細節。

### `arch/x86_64/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | x86_64 architecture layer 說明。 | 邊界文件。 |
| `boot.S` | BIOS boot sector：取得 E820、讀 kernel、建 1 GiB identity map、進 long mode。 | BIOS direct boot 的最早入口。 |
| `entry.S` | kernel image 入口，設定 boot stack、清 `.bss`、呼叫平台 boot protocol。 | x86_64 kernel entry。 |
| `arch.cpp` | 實作 `shirley::arch` 通用介面。 | ISA adapter。 |
| `cpu.cpp` | CPUID vendor、NX/SSE/CR0 WP 等 CPU 初始化。 | CPU feature bring-up。 |
| `gdt.cpp` | GDT、TSS、ring 3 selector。 | x86 privilege model。 |
| `idt.cpp` | 256-entry IDT、handler registry、例外報告。 | x86 interrupt/exception dispatch。 |
| `interrupt.S` | 256 個 ISR stubs 與暫存器保存/恢復入口。 | assembly interrupt entry。 |
| `segment.S` | `lgdt`、`lidt`、`ltr` wrapper 與 `iretq` user-mode transition。 | descriptor/privilege helper。 |
| `paging.cpp` | 四層 page table，實作 `memory::AddressSpace`。 | x86 virtual memory backend。 |
| `internal.hpp` | x86_64 architecture layer 內部宣告。 | 私有介面。 |

### `arch/arm64/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | ARM64 architecture layer 說明。 | 邊界文件。 |
| `entry.S` | AArch64 kernel entry，設定 stack、清 `.bss`、呼叫 boot protocol。 | ARM64 kernel entry。 |
| `arch.cpp` | 實作 `shirley::arch` 通用介面。 | ISA adapter。 |
| `cpu.cpp` | MIDR_EL1 vendor decoding、EL0 FP/SIMD access 設定。 | CPU feature bring-up。 |
| `exception.S` | 2 KiB-aligned EL1 exception vector table 與暫存器保存/恢復。 | ARM exception entry。 |
| `exception.cpp` | 安裝 vector、handler registry、fault reporting。 | ARM exception dispatch。 |
| `paging.cpp` | AArch64 stage 1 translation table，實作 `memory::AddressSpace`。 | ARM64 virtual memory backend。 |
| `internal.hpp` | ARM64 architecture layer 內部宣告。 | 私有介面。 |

## `platform/`

`platform/` 是「機器與韌體」層。它把硬體差異藏在 `include/shirley/platform.hpp` 後面，讓 kernel 不需要用機器名稱做判斷。

### `platform/firmware/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | 韌體資料格式說明。 | firmware data boundary。 |
| `e820.cpp` | 解析 BIOS INT 15h E820 memory map。 | PC BIOS memory map adapter。 |
| `fdt.cpp` | 解析 flattened device tree 的 `/memory` 與 reservation block。 | FDT adapter。 |
| `apple_boot_args.cpp` | 解析 Apple `boot_args` 的 memory、framebuffer、device tree pointer。 | Apple firmware adapter。 |
| `uefi.cpp` | 轉換 UEFI memory map。 | UEFI handoff adapter。 |

### `platform/pc/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | IBM PC 共用硬體說明。 | PC shared hardware layer。 |
| `pic.cpp` | 8259A PIC remap、mask/unmask、spurious IRQ、EOI。 | x86 bring-up interrupt controller。 |
| `pit.cpp` | 8253/8254 PIT，100 Hz IRQ0 timer。 | PC timer driver。 |
| `ps2_keyboard.cpp` | 8042/PS2 keyboard IRQ1，讀 port 0x60 並把字元放入 input queue。 | PC keyboard hardware half。 |
| `serial_console.cpp` | COM1 serial console output backend。 | PC console backend。 |
| `serial_input.cpp` | COM1 IRQ4 receive path。 | PC serial input driver。 |
| `serial_device.cpp` | 把 COM1 發布成 `uart0` device。 | device registry bridge。 |

### `platform/arm/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | ARM 共用硬體說明。 | ARM shared hardware layer。 |
| `gicv2.cpp` | GICv2 distributor/CPU interface，demultiplex device IRQ。 | ARM interrupt controller backend。 |
| `generic_timer.cpp` | ARM architected timer，PPI 30，100 Hz。 | ARM timer driver。 |
| `pl011_input.cpp` | PL011 receive IRQ 與 `uart0` input device。 | ARM serial input driver。 |

### `platform/qemu_x86_64/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | QEMU PC + SeaBIOS 平台說明。 | Tier 1 x86 platform。 |
| `boot_protocol.cpp` | 把 BIOS boot sector 傳來的 E820 table 轉成 `BootInfo`。 | firmware handoff adapter。 |
| `platform.cpp` | 機器識別、capabilities、IRQ routing、ACPI power off。 | platform contract implementation。 |
| `kernel.ld` | kernel link layout，放在 `0x10000`。 | platform memory layout。 |
| `boot.ld` | boot sector link layout，放在 `0x7c00`。 | BIOS boot layout。 |

### `platform/qemu_x86_64_uefi/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | QEMU PC + OVMF 平台說明。 | UEFI x86 platform。 |
| `boot_protocol.cpp` | 驗證 UEFI loader 提供的 `BootHandoff`。 | UEFI kernel handoff adapter。 |
| `platform.cpp` | 機器識別、capabilities、IRQ routing、ACPI power off。 | platform contract implementation。 |
| `kernel.ld` | kernel link layout，放在 2 MiB 附近避開 firmware low memory。 | UEFI platform memory layout。 |

### `platform/qemu_arm64/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | QEMU `virt` ARM64 平台說明。 | Tier 1 ARM64 platform。 |
| `boot_protocol.cpp` | 讀取 QEMU `-kernel` 在 `x0` 傳入的 FDT 並產生 `BootInfo`。 | FDT handoff adapter。 |
| `console.cpp` | PL011 UART console backend。 | ARM64 console output。 |
| `platform.cpp` | 機器識別、capabilities、GIC/timer bring-up、PSCI power。 | platform contract implementation。 |
| `kernel.ld` | kernel link layout，放在 `0x40080000`。 | platform memory layout。 |

### `platform/qemu_arm64_uefi/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | QEMU `virt` + AAVMF/EDK2 平台說明。 | UEFI ARM64 platform。 |
| `boot_protocol.cpp` | 驗證 UEFI loader 提供的 `BootHandoff`。 | UEFI kernel handoff adapter。 |
| `platform.cpp` | 機器識別、capabilities、PSCI power；重用 QEMU ARM64 裝置設定。 | platform contract implementation。 |
| `kernel.ld` | kernel link layout，放在 RAM 起點上方 16 MiB。 | UEFI platform memory layout。 |

### `platform/apple_silicon/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | Apple Silicon 平台狀態與限制。 | ARM64 platform, build-only target。 |
| `boot_protocol.cpp` | 讀取 `x0` 的 Apple `boot_args`。 | Apple firmware handoff adapter。 |
| `console.cpp` | Samsung S5L-derived debug UART。 | Apple console backend。 |
| `interrupt_controller.cpp` | Apple AIC v1 mask/unmask/demux/EOI。 | Apple interrupt controller backend。 |
| `platform.cpp` | 機器識別、capabilities、IRQ routing、MMIO 列表。 | platform contract implementation。 |
| `kernel.ld` | Apple target kernel layout。 | platform memory layout。 |
| `internal.hpp` | Apple platform 內部宣告與 MMIO base。 | 私有介面。 |

## `boot/`

`boot/` 執行在韌體環境，不屬於 kernel runtime。它的輸出是可驗證的 `BootHandoff` 或 kernel entry handoff。

### `boot/common/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | boot loader 共用邏輯說明。 | loader shared layer。 |
| `elf64.cpp` | 驗證 kernel ELF64，列出 `PT_LOAD` segments 與 physical extent。 | loader ELF reader。 |
| `runtime.cpp` | loader freestanding `memcpy`/`memset` 等 runtime。 | firmware-side compiler runtime。 |

### `boot/uefi/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | UEFI loader 流程與限制。 | UEFI loader design doc。 |
| `uefi.hpp` | ShirleyOS loader 使用的 UEFI type 與 protocol subset。 | firmware ABI declarations。 |
| `main.cpp` | 讀 `\shirley\kernel.elf`、載入 segment、收 memory map、`ExitBootServices`、跳 kernel。 | production UEFI boot path。 |

### `boot/apple/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | Apple Silicon boot integration 預留說明。 | future Apple loader slot。 |

## `kernel/`

`kernel/` 是 generic kernel，不直接知道 CPU register、firmware table 或具體硬體 register。

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `kernel_main.cpp` | kernel 第一個 C++ 主流程：初始化各核心服務、掛 rootfs/devfs、啟動 shell/init。 | generic kernel orchestrator。 |

### `kernel/console/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | generic console 設計說明。 | console boundary doc。 |
| `console.cpp` | console backend selection、輸出、`console` device 註冊。 | hardware-independent console service。 |
| `console_input.cpp` | 管理多個 input device，讓 console read 合併 kbd/uart 等來源。 | input multiplexing layer。 |

### `kernel/device/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | device abstraction 與 registry 說明。 | driver/user contract doc。 |
| `device.cpp` | device registry、查找、註冊/移除、`ByteStream` adapter。 | device namespace core。 |
| `null.cpp` | `/dev/null` 對應的 `null` device。 | minimal pseudo device。 |

### `kernel/elf/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | user ELF loader 說明。 | loader doc。 |
| `user_loader.cpp` | 驗證 user ELF、配置 user pages、映射 PT_LOAD、建立 stack。 | user program image loader。 |

### `kernel/user/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `launch.cpp` | 從 VFS 讀 ELF，建立 address space，映射 kernel/platform MMIO，進 user mode，回收。 | kernel-to-userspace launcher。 |

### `kernel/syscall/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | syscall dispatcher 設計。 | syscall ABI doc。 |
| `syscall.cpp` | 解碼 `syscall::Context`，分派 write/read/open/close/exit/stat/list/uptime/exec/mount/block_read。 | kernel syscall dispatcher。 |

### `kernel/process/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | process/file descriptor/scheduler 說明。 | process doc。 |
| `process.cpp` | 單一 user process 的 FD table，將 0/1/2 接 console，其餘接 VFS descriptor。 | process I/O layer。 |
| `scheduler.cpp` | 固定表格 cooperative scheduler。 | early generic scheduler。 |

### `kernel/memory/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | generic memory management 說明。 | memory doc。 |
| `page_allocator.cpp` | 根據 `BootInfo` 管理 physical page extents，配置/釋放 4 KiB page。 | physical memory allocator。 |

### `kernel/interrupt/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | generic IRQ routing 說明。 | interrupt boundary doc。 |
| `irq.cpp` | `shirley::irq` request/release/dispatch，串接 platform IRQ 與 arch vector。 | generic IRQ service。 |

### `kernel/fs/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | VFS、SHRFS1、devfs 說明。 | storage namespace doc。 |
| `vfs.cpp` | path normalization、mount table、open file table、read/write/seek/list/stat。 | global path namespace。 |
| `shrfs.cpp` | SHRFS1 image mount 與低階 read/list/walk。 | read-only root filesystem driver。 |
| `shrfs_vfs.cpp` | 把 SHRFS1 expose 成 `vfs::FileSystem`。 | filesystem-to-VFS adapter。 |
| `devfs.cpp` | 把 device registry expose 成 `/dev`。 | device namespace filesystem。 |
| `rootfs_mount.cpp` | 將建置生成的 rootfs image 包成 RAM disk，掛載並註冊 `ram0`。 | early root storage bring-up。 |

### `kernel/io/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `console_stream.cpp` | 把 console 包成 `ByteStream`，連到 stdout/stderr。 | standard stream backend。 |
| `input_queue.cpp` | interrupt handler 與消費者之間的 bounded ring buffer。 | input buffering primitive。 |
| `standard_streams.cpp` | 管理 kernel standard input/output/error 指標。 | generic stdio-like routing。 |

### `kernel/shell/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | kernel shell 設計與命令說明。 | shell doc。 |
| `shell.cpp` | kernel resident shell，提供 `ls`、`cat`、`cd`、`devices`、`blk`、`exec` 等命令。 | boot console user interface。 |

### `kernel/format/`, `kernel/text/`, `kernel/runtime/`, `kernel/freestanding/`

| 路徑/檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `kernel/format/README.md` | number formatting 設計。 | utility doc。 |
| `kernel/format/number.cpp` | bounded decimal/hex formatting。 | diagnostics utility。 |
| `kernel/text/README.md` | bounded string helpers 設計。 | utility doc。 |
| `kernel/text/string.cpp` | `length`、`equals`、`copy`、`append`。 | freestanding string utility。 |
| `kernel/runtime/README.md` | compiler runtime 支援說明。 | runtime doc。 |
| `kernel/runtime/compiler_runtime.cpp` | `memcpy`、`memset`、`memmove`、`memcmp`、delete/pure virtual traps。 | kernel compiler runtime。 |
| `kernel/freestanding/cstddef` | freestanding `<cstddef>` shim。 | libc-free header shim。 |
| `kernel/freestanding/cstdint` | freestanding `<cstdint>` shim。 | libc-free header shim。 |
| `kernel/freestanding/limits` | freestanding `<limits>` shim。 | libc-free header shim。 |
| `kernel/freestanding/array` | freestanding `<array>` shim。 | libc-free header shim。 |

## `drivers/`

`drivers/` 放跨平台或硬體無關的 driver pieces。碰硬體 register/port 的程式仍放在 `platform/`。

| 路徑/檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | driver 分層規則。 | driver boundary doc。 |
| `drivers/block/ram_disk.cpp` | 以一段記憶體實作 `BlockDevice`。 | early storage backend。 |
| `drivers/input/README.md` | input driver 分層說明。 | input driver doc。 |
| `drivers/input/scancode.cpp` | PS/2 scancode set 1 到 ASCII 的硬體無關解碼。 | testable input translation。 |

## `include/shirley/`

`include/shirley/` 是各層的公開 contract。一般規則是：`kernel/` 只能看 generic header；`arch/<isa>/` 與對應平台才看 `include/shirley/arch/<isa>/`。

### Core contracts

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `arch.hpp` | generic kernel 對 ISA 的完整介面。 | architecture abstraction。 |
| `platform.hpp` | generic kernel 對 machine/platform 的完整介面。 | platform abstraction。 |
| `boot_info.hpp` | 韌體/loader 正規化後的 memory/framebuffer/module 描述。 | boot data contract。 |
| `boot_protocol.hpp` | ShirleyOS loader 與 kernel 的 `BootHandoff` ABI。 | loader-kernel ABI。 |
| `address_space.hpp` | 虛擬/實體位址與 page permission、`AddressSpace` interface。 | virtual memory contract。 |
| `memory.hpp` | physical page allocator API。 | memory service API。 |
| `irq.hpp` | driver 使用的 IRQ request/dispatch API。 | interrupt service API。 |
| `syscall.hpp` | syscall number、context 與 dispatcher 宣告。 | user-kernel ABI。 |
| `user_api.hpp` | syscall 回傳給 user 的 `NodeInfo`、`UptimeInfo`、`MountInfo` 等資料結構。 | user-visible data ABI。 |

### Kernel service contracts

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `console.hpp` | console backend、write/read、input attachment。 | console service API。 |
| `device.hpp` | `Device`、`Operations`、registry、stream adapter。 | device contract。 |
| `io.hpp` | `ByteStream`、`Result`、standard streams。 | byte I/O contract。 |
| `input_queue.hpp` | bounded input queue class。 | input buffering API。 |
| `block_device.hpp` | sector/block device interface。 | storage device contract。 |
| `ram_disk.hpp` | RAM-backed `BlockDevice` class。 | early storage API。 |
| `fs.hpp` | SHRFS low-level API。 | root filesystem API。 |
| `vfs.hpp` | VFS node、filesystem interface、open/read/write/list/stat API。 | global path API。 |
| `rootfs.hpp` | generated rootfs image 與 mount helper。 | rootfs mount API。 |
| `process.hpp` | user process FD table API。 | process I/O API。 |
| `scheduler.hpp` | cooperative task scheduler API。 | early scheduling API。 |
| `shell.hpp` | kernel shell entry。 | interactive kernel UI API。 |
| `text.hpp` | bounded string helper API。 | text utility API。 |
| `format.hpp` | bounded number formatting API。 | diagnostics formatting API。 |
| `user_loader.hpp` | user ELF loading/launching API。 | user execution API。 |

### Architecture/platform-specific headers

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `arch/x86_64/port_io.hpp` | `inb/outb/inw/outw/io_wait`。 | x86 port I/O primitive。 |
| `arch/x86_64/paging.hpp` | x86_64 page table class。 | x86 VM public type。 |
| `arch/arm64/exception.hpp` | ARM64 EL1 vector entry numbers。 | ARM exception ABI。 |
| `arch/arm64/paging.hpp` | ARM64 page table class。 | ARM VM public type。 |
| `boot/elf64.hpp` | boot loader ELF64 reader API。 | loader shared contract。 |
| `input/scancode.hpp` | PS/2 scancode decoder API。 | hardware-independent input API。 |
| `platform/firmware/e820.hpp` | E820 parser API。 | BIOS data API。 |
| `platform/firmware/fdt.hpp` | FDT parser API。 | device-tree data API。 |
| `platform/firmware/apple_boot_args.hpp` | Apple `boot_args` parser API。 | Apple firmware data API。 |
| `platform/firmware/uefi.hpp` | UEFI memory map conversion API。 | UEFI data API。 |
| `platform/pc/pic.hpp` | 8259A PIC API。 | PC interrupt controller API。 |
| `platform/pc/pit.hpp` | PIT timer API。 | PC timer API。 |
| `platform/pc/ps2_keyboard.hpp` | PS/2 keyboard driver API。 | PC keyboard API。 |
| `platform/pc/serial.hpp` | COM1 console/input/device API。 | PC serial API。 |
| `platform/arm/gicv2.hpp` | GICv2 driver API。 | ARM IRQ controller API。 |
| `platform/arm/generic_timer.hpp` | ARM generic timer API。 | ARM timer API。 |
| `platform/arm/pl011.hpp` | PL011 input/device API。 | ARM serial API。 |

## `libc/`

`libc/` 是 user program 使用的 C runtime。它不等於 host libc，也不應該依賴作業系統外部功能。

| 路徑/檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | libc scope 說明。 | user runtime doc。 |
| `include/stdio.h` | `printf` 宣告。 | user C API。 |
| `include/unistd.h` | `read/write/close/_exit` 與 `ssize_t` 宣告。 | user POSIX-like API。 |
| `include/fcntl.h` | `open` 與 `O_RDONLY/O_WRONLY/O_RDWR`。 | user file API。 |
| `src/start_x86_64.S` | x86_64 `_start`，呼叫 `main` 後 syscall exit。 | x86 user runtime entry。 |
| `src/start_arm64.S` | ARM64 `_start`，呼叫 `main` 後 syscall exit。 | ARM user runtime entry。 |
| `src/stdio/README.md` | stdio placeholder/doc。 | libc component doc。 |
| `src/stdio/printf.c` | 小型 fixed-buffer `printf`，最後寫 fd 1。 | user formatted output。 |
| `src/unistd/write.c` | `write` syscall wrapper。 | user-kernel wrapper。 |
| `src/unistd/read.c` | `read` syscall wrapper。 | user-kernel wrapper。 |
| `src/unistd/open.c` | `open` syscall wrapper。 | user-kernel wrapper。 |
| `src/unistd/close.c` | `close` syscall wrapper。 | user-kernel wrapper。 |
| `src/unistd/_exit.c` | `exit` syscall wrapper，不返回。 | process termination wrapper。 |
| `src/string/README.md` | string placeholder/doc。 | future libc component。 |
| `src/stdlib/README.md` | stdlib placeholder/doc。 | future libc component。 |
| `arch/x86_64/syscall.S` | x86_64 `shirley_syscall`，用 `int 0x80` trap。 | x86 syscall trampoline。 |
| `arch/arm64/syscall.S` | ARM64 `shirley_syscall`，用 `svc #0` trap。 | ARM syscall trampoline。 |

## `user/`

`user/` 內的程式會被 cross-compile 成 static ELF，再由 build system 打包進 rootfs 的 `/bin`。

### `user/hello/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | hello user program 說明。 | user app doc。 |
| `main.c` | 印問候，透過 syscall 讀 `/etc/version`，返回 shell。 | minimal user-space program。 |
| `x86_64.ld` | x86_64 user ELF linker script。 | user program layout。 |
| `arm64.ld` | ARM64 user ELF linker script。 | user program layout。 |

### `user/init/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `main.c` | user-space shell/init，提供 `help/cd/pwd/echo/clear/ls/cat/stat/mount/uptime/blk/exec/hello`。 | future `/bin/init` and user shell。 |

## `rootfs/`

`rootfs/` 是開機時會看到的 `/`。建置時 `cmake/make-rootfs.cmake` 把它和 `bin/hello`、`bin/init` 等 build artifacts 打包成 SHRFS1 image。

| 路徑/檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | rootfs 說明，也可在 ShirleyOS 裡 `cat /README.md`。 | root filesystem doc/file。 |
| `docs/filesystem.md` | 檔案系統使用說明。 | in-OS documentation。 |
| `docs/shell.md` | shell 使用說明。 | in-OS documentation。 |
| `etc/motd` | shell 啟動訊息。 | boot user-facing content。 |
| `etc/version` | OS 版本字串。 | user-visible system identity。 |
| `home/shirley/notes.txt` | 範例 home file。 | sample filesystem content。 |

## `cmake/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `toolchain-x86_64.cmake` | clang `x86_64-none-elf` bare-metal toolchain。 | cross build setup。 |
| `toolchain-arm64.cmake` | clang `aarch64-none-elf` bare-metal toolchain。 | cross build setup。 |
| `make-rootfs.cmake` | 把 `rootfs/` 和 extra artifacts 打包成 SHRFS1 C++ byte array。 | rootfs image builder。 |
| `make-disk-image.cmake` | 串接 boot sector 與 kernel，產生 BIOS disk image。 | x86 BIOS image builder。 |
| `check-image-size.cmake` | 檢查 x86 BIOS image 不超過 boot sector 一次可讀大小。 | build-time safety check。 |

## `scripts/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `check-deps.sh` | 檢查 CMake、LLVM、LLD、QEMU 等 host tool。 | development environment check。 |
| `build.sh` | 依 target 設定 toolchain、build dir、artifact，執行 CMake build。 | primary build script。 |
| `build-x86_64.sh` | `build.sh x86_64` wrapper。 | convenience build wrapper。 |
| `build-arm64.sh` | `build.sh arm64` wrapper。 | convenience build wrapper。 |
| `build-apple-silicon.sh` | `build.sh apple_silicon` wrapper。 | build-only target wrapper。 |
| `run-x86_64.sh` | 建置並用 QEMU SeaBIOS 啟動 x86_64 image。 | emulator run script。 |
| `run-x86_64_uefi.sh` | 建置並用 OVMF 啟動 x86_64 UEFI target。 | emulator run script。 |
| `run-arm64.sh` | 建置並用 QEMU `virt -kernel` 啟動 ARM64 kernel。 | emulator run script。 |
| `run-arm64_uefi.sh` | 建置並用 AAVMF/EDK2 啟動 ARM64 UEFI target。 | emulator run script。 |
| `debug-x86_64.sh` | x86_64 QEMU 暫停並開 `localhost:1234` GDB server。 | emulator debug script。 |
| `debug-x86_64_uefi.sh` | x86_64 UEFI QEMU debug。 | emulator debug script。 |
| `debug-arm64.sh` | ARM64 QEMU debug。 | emulator debug script。 |
| `debug-arm64_uefi.sh` | ARM64 UEFI QEMU debug。 | emulator debug script。 |
| `find-uefi-firmware.sh` | 在常見路徑尋找 OVMF/AAVMF firmware。 | host firmware discovery。 |
| `test-all.sh` | 跑 host tests，建置各 target，QEMU boot 後打字驗證 shell/VFS/IRQ。 | integration test runner。 |

## `tests/`

`tests/` 只測不需要 privileged instruction 的部分；完整開機行為由 `scripts/test-all.sh` 用 QEMU 驗證。

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `README.md` | 測試範圍與 QEMU stage 說明。 | test strategy doc。 |
| `host_smoke.cpp` | 測 physical page allocator 與 invalid `BootInfo`。 | memory contract test。 |
| `core_services_smoke.cpp` | 測 scheduler、standard streams、RAM disk。 | core service test。 |
| `platform_model_smoke.cpp` | 測 E820、FDT、Apple `boot_args` parsing。 | firmware adapter test。 |
| `boot_loader_smoke.cpp` | 測 ELF64 reader、UEFI memory map conversion、BootHandoff validation。 | boot contract test。 |
| `input_smoke.cpp` | 測 PS/2 scancode decoder 與 input queue。 | input contract test。 |
| `device_smoke.cpp` | 測 device registry、failure cases、console input multiplexing。 | device contract test。 |
| `vfs_smoke.cpp` | 測 path normalization、mount table、open/read/write/seek/close、devfs、block read。 | VFS contract test。 |
| `file_system_smoke.cpp` | 測 text helpers 與 rootfs SHRFS image mount/walk/list/read。 | filesystem contract test。 |

## `docs/`

| 檔案 | 作用 | 架構地位 |
| --- | --- | --- |
| `architecture.md` | 簡短說明 `arch`、`platform`、`drivers`、`device`、`console`、`fs` 的單向依賴。 | architecture boundary note。 |

## 目前可延伸的方向

| 方向 | 主要會碰到的目錄 | 架構注意事項 |
| --- | --- | --- |
| 新增 CPU 架構，例如 RISC-V64 | `arch/riscv64/`, `include/shirley/arch/riscv64/`, `cmake/` | 實作 `arch.hpp` contract，不把機器裝置放進 `arch/`。 |
| 新增硬體平台 | `platform/<platform>/`, 可能重用 `platform/pc/` 或 `platform/arm/` | 將韌體資料轉成 `BootInfo`，提供 capabilities、IRQ routing、MMIO list。 |
| 新增實體磁碟 driver | `drivers/block/` 或 `platform/<machine>/` | 對上 expose `BlockDevice`，讓 SHRFS/VFS 不需要知道硬體。 |
| 新增可寫檔案系統 | `kernel/fs/`, `include/shirley/vfs.hpp` | 實作 `vfs::FileSystem`，不要讓 VFS 依賴單一格式。 |
| 擴充 user ABI | `include/shirley/syscall.hpp`, `kernel/syscall/`, `libc/`, `user/` | syscall number、kernel dispatcher、libc wrapper 必須一致。 |
| 真正啟動 Apple Silicon | `platform/apple_silicon/`, `boot/apple/`, `platform/firmware/` | 需要 Apple device tree parser、timer、SMC/PMU、實機 loader。 |
| 從 kernel shell 過渡到 `/bin/init` | `kernel/kernel_main.cpp`, `user/init/`, `kernel/user/`, `kernel/process/` | shell 應走 user-space syscall/VFS，不應繞過 process ABI。 |
