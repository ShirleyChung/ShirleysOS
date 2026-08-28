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
platform/ qemu_x86_64/  pc/  qemu_arm64/  arm/  apple_silicon/  firmware/
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

`platform/` 底下有三個目錄不代表機器：

Three `platform/` directories are not machines:

* `platform/pc/` 存放所有 IBM PC 相容機器共用的硬體：8259A 中斷控制器、
  8253/8254 計時器、PS/2 鍵盤與 COM1 序列主控台。
* `platform/arm/` 是它在 ARM 這邊的對應：存放由 ARM 定義、而不是由某一台
  機器定義的硬體，目前是 GICv2 中斷控制器與架構計時器。Apple Silicon 用的是
  自家的 AIC，不使用這個目錄。
* `platform/firmware/` 存放**韌體資料格式**，同一種格式可能出現在多台機器上：
  BIOS E820 記憶體地圖、flattened device tree、Apple `boot_args`，以及 UEFI
  記憶體地圖。這些都是純粹的資料轉換，不碰任何硬體，因此會編進主機建置並由
  測試涵蓋。

* `platform/pc/` holds hardware shared by every IBM-PC-compatible machine: the
  8259A interrupt controller, the 8253/8254 timer, the PS/2 keyboard, and the
  COM1 serial console.
* `platform/arm/` is its counterpart on the ARM side: hardware ARM defines
  rather than any one machine, today a GICv2 interrupt controller and the
  architected timer. Apple Silicon uses its own AIC and none of this
  directory.
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
永遠不必用機器名稱做判斷）、中斷控制器的遮罩、假中斷判斷與 end-of-interrupt、
平台 IRQ 到架構向量的對應、平台計時器的 tick 計數與頻率，以及關機與重開機。

`include/shirley/platform.hpp` covers machine identity, a `Capabilities`
record so generic code never tests for a machine by name, interrupt-controller
masking, spurious-interrupt detection, and end-of-interrupt, the platform IRQ
to architecture vector mapping, the platform timer's tick count and rate, and
power off/restart.

每個平台另外提供 `shirley_platform_boot_info()`，把韌體交給核心的任何資料轉換成
`include/shirley/boot_info.hpp` 中與機器無關的 `BootInfo`。

Each platform also supplies `shirley_platform_boot_info()`, which converts
whatever the firmware handed the kernel into the neutral `BootInfo` in
`include/shirley/boot_info.hpp`.

平台還會列出 `MmioRegion`：核心在其他位址空間執行時仍然會碰到的裝置記憶體。
進入 user 空間會換上 user 的分頁表，但中斷不會因此停止：處理常式要讀中斷
控制器，主控台也還要能輸出，這些 MMIO 因此必須存在於每一個位址空間裡，
否則第一個在 user 空間收到的中斷就是一次 data abort。這件事只有平台知道，
所以由平台列出，通用的 `kernel/user/launch.cpp` 只負責照著映射。裝置掛在
I/O port 上的平台（PC 的 8259A、PIT、COM1）回報零個區段，因為 port I/O 不
經過分頁表。

A platform also lists its `MmioRegion`s: the device memory the kernel still
touches while another address space is active. Entering userspace switches to
the user's page tables, but interrupts do not stop: a handler reads the
interrupt controller and the console still prints, so that MMIO has to exist in
every address space or the first interrupt taken in userspace is a data abort.
Only the platform knows which addresses those are, so the platform lists them
and the generic `kernel/user/launch.cpp` merely maps what it is told. A
platform whose devices are behind I/O ports — the PC's 8259A, PIT, and COM1 —
reports no regions at all, because port I/O does not go through the page
tables.

## 中斷子系統 / Interrupt subsystem

中斷路徑分成三層，每一層都可以獨立替換：架構層擁有向量表，平台層擁有中斷
控制器，通用的 IRQ 層把兩者接起來讓驅動程式使用。驅動程式只認得平台 IRQ
編號，永遠不會直接碰到中斷控制器。

The interrupt path has three layers, each replaceable on its own: the
architecture layer owns the vector table, the platform layer owns the
interrupt controller, and a generic IRQ layer joins them for drivers to use.
A driver knows only a platform IRQ number and never touches an interrupt
controller directly.

```text
裝置 / device -> 中斷控制器 / interrupt controller -> 架構向量 / architecture vector
             -> shirley::irq::dispatch() -> 驅動程式處理常式 / driver handler
             -> end-of-interrupt
```

### x86_64 IDT

`arch/x86_64/` 建立完整的 256 項中斷描述元表。`interrupt.S` 產生 256 個等長的
入口，IDT 因此可以用 `x86_64_isr_stubs + vector * stride` 直接算出每個入口的
位址；會由 CPU 自行推入錯誤碼的向量不再補上假的錯誤碼，讓堆疊配置在兩種情況
下完全一致。每個入口都是 DPL 0 的 64 位元中斷閘，使用核心程式碼段選擇子，
進入處理常式時自動清除 IF，因此中斷處理常式不會被巢狀中斷打斷。入口會依
`InterruptFrame` 的欄位順序保存所有通用暫存器，在呼叫 C++ 分派函式前讓堆疊
維持 System V AMD64 要求的 16 位元組對齊，最後以 `iretq` 返回。

`arch/x86_64/` installs a full 256-entry interrupt descriptor table.
`interrupt.S` generates 256 equally sized entry points, so the IDT can compute
each one as `x86_64_isr_stubs + vector * stride`; a vector where the CPU
pushes its own error code does not push a dummy one, keeping the stack layout
identical either way. Every entry is a 64-bit interrupt gate at DPL 0 using
the kernel code selector, which clears IF on entry so a handler is never
interrupted by a nested interrupt. The entry saves every general-purpose
register in `InterruptFrame` field order, leaves the stack at the 16-byte
alignment System V AMD64 requires before calling the C++ dispatcher, and
returns with `iretq`.

向量 0-31 是 CPU 例外，沒有註冊處理常式時會印出暫存器內容並停住處理器。
向量 32 以上是裝置中斷，沒有註冊處理常式時直接忽略。

Vectors 0-31 are CPU exceptions and, with no handler registered, print a
register dump and stop the processor. Vectors 32 and above are device
interrupts and are ignored when nothing is registered.

### 8259A 啟用用後端 / the 8259A bring-up backend

8259A 是 x86_64 的**啟用用**中斷控制器後端，不是長期的中斷架構。它的優點只
有一個：不需要 ACPI、不需要列舉，開機就能用，因此適合把中斷路徑先跑通。

The 8259A is x86_64's interrupt controller backend **for bring-up**, not the
long-term interrupt architecture. Its single advantage is that it needs no
ACPI and no enumeration and works from the moment the machine boots, which
makes it the right thing to get the interrupt path working end to end.

`platform/pc/pic.cpp` 把兩顆串接的控制器重新對應，讓 IRQ0-7 落在向量
0x20-0x27、IRQ8-15 落在 0x28-0x2f，避開被 CPU 例外佔用的前 32 個向量。開機
後除了 IRQ2 串接線之外，所有裝置 IRQ 都是遮罩狀態，由各驅動程式自行解除；
串接線本身不是裝置中斷，遮住它會讓整顆從控制器都送不出中斷。

`platform/pc/pic.cpp` remaps the two cascaded controllers so IRQ0-7 land on
vectors 0x20-0x27 and IRQ8-15 on 0x28-0x2f, clear of the first 32 vectors the
CPU exceptions occupy. After boot every device IRQ is masked except the IRQ2
cascade line, and each driver unmasks its own; the cascade is not a device
interrupt, and masking it would silence the whole slave controller.

8259A 有一個必須處理的行為：中斷訊號在確認週期前消失時，控制器仍會送出
IRQ7 或 IRQ15。這種假中斷不能執行處理常式，也不能送出 end-of-interrupt，
否則會提早結束下一個真正的中斷。判斷方式是讀取控制器的服務中暫存器，
從控制器的假中斷則仍須替它向主控制器送出 end-of-interrupt。

The 8259A has one behaviour that must be handled: when the interrupt signal
disappears before the acknowledge cycle, the controller still reports IRQ7 or
IRQ15. Such a spurious interrupt must run no handler and must receive no
end-of-interrupt, which would otherwise prematurely finish the next genuine
interrupt. It is detected by reading the controller's in-service register, and
a spurious slave interrupt still owes the master an end-of-interrupt.

### IRQ 抽象層 / the IRQ abstraction

`include/shirley/irq.hpp` 是驅動程式看到的唯一中斷介面：

`include/shirley/irq.hpp` is the only interrupt interface a driver sees:

```c++
bool shirley::irq::request(unsigned irq, Handler handler, void* context = nullptr);
void shirley::irq::release(unsigned irq);
void shirley::irq::dispatch(unsigned irq);
```

`request()` 透過 `platform::irq_vector()` 取得架構向量、把自己掛上去，然後解除
該 IRQ 的遮罩；`dispatch()` 先詢問平台這是否為假中斷，執行處理常式，最後送出
end-of-interrupt。驅動程式因此完全不知道背後是哪一種控制器。

`request()` asks `platform::irq_vector()` for the architecture vector, hooks
itself onto it, and unmasks the IRQ; `dispatch()` first asks the platform
whether the interrupt was spurious, runs the handler, and finally signals
end-of-interrupt. A driver therefore never learns which controller is behind
it.

這一層同時支援兩種控制器形狀，差別由 `platform::irq_vector()` 表達：

The layer supports both controller shapes, and `platform::irq_vector()` is
what tells them apart:

* **一個 IRQ 一個向量**：8259A 與之後的 IOAPIC 屬於這一類。`irq_vector()`
  回傳真正的向量編號，IRQ 層自己把處理常式掛上去。
* **集中到單一向量**：GIC 與 Apple AIC 屬於這一類，所有裝置中斷都落在同一個
  IRQ 例外入口。`irq_vector()` 回傳 `platform::demultiplexed_vector`，IRQ 層
  就不會去掛向量；那個向量由控制器驅動程式親自佔用，讀取硬體暫存器
  （GIC 是 `GICC_IAR`，AIC 是 EVENT）問出實際來源，再呼叫 `dispatch()`。
  這個區別不是形式：如果 IRQ 層照樣為每個 IRQ 掛向量，所有驅動程式都會掛到
  同一個向量互相覆蓋，還會蓋掉控制器自己的分辨常式。

* **One vector per IRQ**: the 8259A and a later IOAPIC. `irq_vector()` returns
  a real vector number and the IRQ layer hooks the handler itself.
* **Funnelled into one vector**: a GIC and Apple's AIC, where every device
  interrupt lands on the same IRQ exception entry. `irq_vector()` returns
  `platform::demultiplexed_vector`, so the IRQ layer hooks nothing; the
  controller driver owns that vector, reads a hardware register — `GICC_IAR`
  on a GIC, EVENT on the AIC — to learn the real source, and calls
  `dispatch()`. The distinction is not a formality: were the IRQ layer to hook
  a vector per IRQ anyway, every driver would land on the same vector and
  overwrite both each other and the controller's own demultiplexing handler.

編號超出處理常式表範圍的中斷仍然會走完 end-of-interrupt。控制器已經確認過
這個中斷，不回覆它就會讓控制器永遠停在服務中狀態。

An interrupt numbered past the end of the handler table still receives its
end-of-interrupt. The controller has already taken it, and leaving it
unanswered parks the controller in service for good.

### PIT 計時器 / the PIT timer

`platform/pc/pit.cpp` 把 8253/8254 的通道 0 設定成除頻器模式，預設 100 Hz，
接在 IRQ0。計時器中斷本身只累加 tick，不輸出任何訊息：每秒 100 次的日誌會
把主控台淹沒。`platform::timer_ticks()` 與 `platform::timer_frequency()` 是
通用程式碼查詢計時器的方式，沒有計時器驅動程式的平台一律回報 0。

`platform/pc/pit.cpp` programs channel 0 of the 8253/8254 as a rate generator
at 100 Hz by default, on IRQ0. The timer interrupt only counts ticks and
prints nothing: a message a hundred times a second would drown the console.
`platform::timer_ticks()` and `platform::timer_frequency()` are how generic
code queries the timer, and a platform with no timer driver reports zero.

### PS/2 鍵盤 / the PS/2 keyboard

鍵盤分成兩半。掃描碼解碼完全不碰硬體，因此放在
`drivers/input/scancode.cpp`，會編進主機建置並由測試涵蓋；讀取連接埠的部分
放在 `platform/pc/ps2_keyboard.cpp`。驅動程式初始化 8042 控制器、確認第一個
連接埠會產生 IRQ1、清空韌體留下的位元組，然後透過 IRQ 層註冊處理常式。

The keyboard is split in two. Scancode decoding touches no hardware, so it
lives in `drivers/input/scancode.cpp`, compiles into the host build, and is
covered by tests; the part that reads ports lives in
`platform/pc/ps2_keyboard.cpp`. The driver initializes the 8042 controller,
makes sure the first port raises IRQ1, drains whatever the firmware left
behind, and registers its handler through the IRQ layer.

第一版支援掃描碼組 1 的小寫字母、數字、Enter、Backspace、Tab、空白與基本
標點；放開事件與 Shift、Ctrl、Alt 一律忽略，擴充鍵的兩個位元組整組丟棄。
解出的字元會推進 `io::console_input()` 這個共用佇列，驅動程式本身不做回顯：
畫面上該出現什麼是行編輯器才知道的事。建置時定義 `SHIRLEY_DEBUG_SCANCODES`
會印出每一個原始掃描碼。

The first version covers scancode set 1's lowercase letters, digits, Enter,
Backspace, Tab, space, and basic punctuation; release events and Shift, Ctrl,
and Alt are ignored, and both bytes of an extended key are dropped. A decoded
character is pushed into the shared `io::console_input()` queue, and the driver
does not echo: what should appear on screen is something only the line editor
knows. Defining `SHIRLEY_DEBUG_SCANCODES` at build time prints every raw
scancode.

`io::InputQueue` 只有一個生產者（中斷處理常式）與一個消費者（一般核心
程式），生產者只寫 head、消費者只寫 tail，因此不需要鎖。佇列滿了會捨棄新的
字元，生產者不能去動消費者持有的索引。

`io::InputQueue` has exactly one producer (the interrupt handler) and one
consumer (ordinary kernel code); the producer only writes head and the
consumer only writes tail, so no lock is needed. A full queue drops the new
character, because the producer must not touch the index the consumer owns.

### 主控台輸入 / Console input

一台機器可能有好幾個輸入裝置，它們都應該能驅動同一個主控台。所有輸入驅動
程式因此把解碼後的字元推進 `io::console_input()` 這一個共用佇列，並在自己
初始化成功後呼叫 `io::attach_console_input()` 把它接成標準輸入。多個裝置共用
一個佇列不違反單生產者的前提：中斷處理常式執行時中斷是關閉的，兩個處理常式
不會同時跑。

A machine can have several input devices and all of them should be able to
drive the same console. Every input driver therefore pushes its decoded
characters into the one shared `io::console_input()` queue and calls
`io::attach_console_input()` once it is up. Several devices sharing one queue
does not break the single-producer premise: an interrupt handler runs with
interrupts disabled, so two of them never run at the same time.

目前有三個輸入驅動程式。`platform/pc/ps2_keyboard.cpp` 是 IRQ1 上的 PS/2
鍵盤；`platform/pc/serial_input.cpp` 是 IRQ4 上的 COM1 接收路徑，讓序列埠
另一端的終端機也能打字；`platform/arm/pl011_input.cpp` 是 PL011 的接收中斷，
那是 QEMU virt 上唯一的輸入裝置。序列裝置會把終端機的回車符換成換行、把 DEL
換成 Backspace，讓進入佇列的字元和鍵盤解出來的完全一樣。

There are three input drivers today. `platform/pc/ps2_keyboard.cpp` is the PS/2
keyboard on IRQ1; `platform/pc/serial_input.cpp` is COM1's receive path on
IRQ4, so a terminal on the other end of the serial line can type too; and
`platform/arm/pl011_input.cpp` is the PL011's receive interrupt, the only input
device on QEMU virt. The serial drivers translate a terminal's carriage return
into a newline and DEL into a backspace, so what enters the queue is identical
to what the keyboard decodes.

兩個序列驅動程式都必須把接收 FIFO 的觸發門檻降到最低。終端機一次只送一個
字元，門檻設在 14 個位元組（COM1 輸出端的預設值）時，按鍵會留在 FIFO 裡等
不到中斷；PL011 還要同時開啟接收逾時中斷，理由相同。

Both serial drivers have to lower the receive FIFO's trigger level. A terminal
sends one character at a time, and at the fourteen-byte level COM1's output
side leaves behind, keystrokes sit in the FIFO waiting for an interrupt that
never arrives; the PL011 needs its receive-timeout interrupt unmasked for the
same reason.

### ARM64 的 GICv2 與架構計時器 / GICv2 and the architected timer on ARM64

`platform/arm/gicv2.cpp` 是 QEMU virt 兩個 ARM64 平台共用的中斷控制器。
GICv2 由分配器與 CPU 介面兩塊暫存器組成：分配器決定哪些中斷送給哪個核心，
CPU 介面是核心確認與結束中斷的窗口。開機時分配器遮罩全部中斷、清掉待處理
狀態、把所有中斷設成同一個中等優先權，並把 CPU 介面的優先權遮罩設得比它
寬鬆，中斷才通得過。SPI 設定為位準觸發並指向開機核心；SGI 與 PPI 是核心
私有的，目標欄位對它們唯讀。

`platform/arm/gicv2.cpp` is the interrupt controller both QEMU virt ARM64
platforms share. A GICv2 is two register blocks: the distributor decides which
interrupts go to which core, and the CPU interface is where a core
acknowledges and finishes one. At boot the distributor masks every interrupt,
clears any pending state, gives every interrupt the same middle priority, and
the CPU interface's priority mask is set looser than that so interrupts pass
at all. SPIs are level-triggered and targeted at the boot core; SGIs and PPIs
are private to a core, and the target field is read-only for them.

分辨來源的迴圈會一直讀 `GICC_IAR`，直到拿到保留編號為止 —— 1023 代表沒有
待處理中斷。一次例外可能涵蓋多個待處理中斷，所以不能只取一個就返回；保留
編號絕對不能送出 end-of-interrupt。

The demultiplexing loop keeps reading `GICC_IAR` until it gets a reserved
number — 1023 means nothing is pending. One exception can cover several
pending interrupts, so taking just one and returning is wrong, and a reserved
number must never be given an end-of-interrupt.

`platform/arm/generic_timer.cpp` 是架構計時器，接在 PPI 30，同樣預設 100 Hz。
它屬於 CPU 而不是機器，透過 `CNTP_*_EL0` 系統暫存器操作，因此每台 ARM64
機器都有。它的中斷輸出是位準而不是脈衝，所以處理常式第一件事就是重新設定
`CNTP_TVAL_EL0`：條件沒有解除的話，處理常式會被無止盡地重新進入。

`platform/arm/generic_timer.cpp` is the architected timer on PPI 30, also at
100 Hz by default. It belongs to the CPU rather than the machine and is driven
through the `CNTP_*_EL0` system registers, so every ARM64 machine has one. Its
interrupt output is a level rather than a pulse, so the first thing the
handler does is rearm `CNTP_TVAL_EL0`: leaving the condition met would
re-enter the handler forever.

GIC 的位址目前是 QEMU virt 的編譯期常數，做法與 `platform/apple_silicon` 對
AIC 基底位址的處理一致。要從裝置樹的中斷控制器節點讀出來，需要
`platform/firmware/fdt.cpp` 提供通用節點查詢，那還沒有。

The GIC addresses are compile-time constants for QEMU virt today, the same
approach `platform/apple_silicon` takes for the AIC base. Reading them from
the device tree's interrupt controller node needs generic node lookup in
`platform/firmware/fdt.cpp`, which does not exist yet.

### Apple AIC

`platform/apple_silicon/interrupt_controller.cpp` 是 AIC 第 1 版（M1／t8103）
的完整路徑：遮罩、解除遮罩、確認，並親自掛上 IRQ 例外入口讀取 EVENT 暫存器
分辨來源。有兩件事值得記住：讀 EVENT 會取走一個事件，因此
`end_of_interrupt` 刻意不去讀它，取事件只由分辨迴圈負責；另外只有事件種類 1
（裝置 IRQ）會被分派出去，IPI 與計時器事件用的是其他種類，必須丟掉而不是
當成 IRQ 交給驅動程式。

`platform/apple_silicon/interrupt_controller.cpp` is a complete AIC version 1
path for M1 (t8103): mask, unmask, acknowledge, and hooking the IRQ exception
entry itself to read the EVENT register and identify the source. Two things
are worth remembering: reading EVENT consumes an event, so `end_of_interrupt`
deliberately does not read it and only the demultiplexing loop does; and only
event type 1, a device IRQ, is dispatched, because IPIs and timer events use
other types and must be dropped rather than handed to a driver as IRQs.

這條路徑從未實際執行過。QEMU 沒有 Apple Silicon 機器模型，暫存器配置來自
Asahi Linux 公開的說明而非原廠文件，確認中斷的精確語意要等 M8 在實機上驗證。

None of this has ever executed. QEMU has no Apple Silicon machine model, the
register layout comes from Asahi Linux's published documentation rather than a
datasheet, and the exact acknowledge semantics need confirming on real
hardware in M8.

### 後續的中斷控制器後端 / later interrupt controller backends

以下都會實作成新的平台層後端，`include/shirley/irq.hpp` 的介面不需要改變：

Each of these becomes a new platform-layer backend behind the unchanged
`include/shirley/irq.hpp` interface:

* Local APIC 與 IOAPIC：從 ACPI MADT 取得 IOAPIC 位址與中斷來源覆寫，把
  全域中斷編號路由到向量，並改用 Local APIC 送出 end-of-interrupt。多處理器
  支援與 IPI 也需要它。這是 x86_64 的長期中斷架構，8259A 屆時只保留為
  在沒有 ACPI 時的退路。
* MSI/MSI-X：PCIe 裝置直接寫入 Local APIC 的訊息位址來遞送中斷，不再佔用
  IOAPIC 的實體接腳。這需要先有 PCI 設定空間存取，屬於 M5。
* ARM64：GICv3 的重分配器與系統暫存器 CPU 介面尚未支援，因此目前只能跑在
  提供 GICv2 相容介面的機器上。Apple Silicon 還缺 AIC 第 2 版（M2 以後的
  SoC）與架構計時器 —— 後者的中斷編號來自 Apple 裝置樹，屬於 M8。
  ARM64 上與 MSI 對應的機制是 GICv2m 或 GICv3 ITS，不是 IOAPIC；IOAPIC 是
  x86 專屬的裝置。

* Local APIC and IOAPIC: take the IOAPIC address and interrupt source
  overrides from the ACPI MADT, route global system interrupt numbers onto
  vectors, and switch end-of-interrupt to the Local APIC. Multiprocessor
  support and IPIs need it too. This is x86_64's long-term interrupt
  architecture, at which point the 8259A remains only as the fallback for a
  machine with no ACPI.
* MSI/MSI-X: a PCIe device delivers an interrupt by writing the Local APIC's
  message address directly, occupying no physical IOAPIC pin. This needs PCI
  configuration space access first and belongs to M5.
* ARM64: a GICv3's redistributors and system-register CPU interface are not
  supported, so today this runs only on a machine offering a GICv2-compatible
  interface. Apple Silicon still lacks AIC version 2 for M2 and later SoCs,
  and the architected timer — whose interrupt number comes from the Apple
  device tree, which belongs to M8. The ARM64 counterpart of MSI is GICv2m or
  the GICv3 ITS, not an IOAPIC; an IOAPIC is an x86-only device.

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

核心以 `-fno-threadsafe-statics` 編譯。函式內的 static 物件預設會產生
`__cxa_guard_*` 呼叫，那是 libstdc++ 的執行期常式，裸機核心沒有可連結的
版本。核心目前只跑在單一核心上，第一次初始化不可能被另一個執行緒插隊，因此
關掉那層保護是安全的；等到有第二顆核心時，這個假設必須重新檢視。

The kernel is compiled with `-fno-threadsafe-statics`. A function-local static
object otherwise emits `__cxa_guard_*` calls, libstdc++ runtime routines a
freestanding kernel has nothing to link against. The kernel runs on a single
core today, so no other thread can race the first initialization and dropping
the guard is safe; that assumption has to be revisited when a second core
appears.

## 檔案系統 / File system

`shirley::fs` 掛載一份唯讀的 SHRFS1 卷冊。映像分成三段：32 位元組的標頭、
每項 80 位元組的目錄表，以及所有檔案內容連續排列的資料區。目錄不儲存子項目
清單，而是每個項目記錄自己所屬目錄的索引，列出目錄就是掃過目錄表挑出父項目
相符的項目；在這個規模下掃描的成本可以忽略，換來的是清單不可能和項目本身
不一致。項目 0 一定是根目錄，而且父項目是自己，因此往上走一定會停下來。

`shirley::fs` mounts one read-only SHRFS1 volume. The image is three regions: a
32-byte header, an 80-byte entry per file or directory, and a data region with
every file's bytes back to back. A directory stores no list of children;
instead each entry records the index of the directory holding it, and listing a
directory is a scan for entries whose parent matches. At this size the scan
costs nothing and in exchange a listing can never disagree with the entries it
names. Entry 0 is always the root and is its own parent, so walking upwards
always terminates.

標頭與項目都逐位元組以小端序解碼，核心的結構排列方式因此不影響能不能讀懂
映像。`mount()` 驗證標頭之後會把每個項目都解碼一次：損壞的映像在掛載時就
失敗，而不是變成第一個 `ls` 的怪異行為。

Both the header and the entries are decoded byte by byte as little-endian, so
the kernel's struct layout cannot change how an image is read. After validating
the header, `mount()` decodes every entry once: a corrupt image fails at mount
time instead of turning into strange behaviour on the first `ls`.

根檔案系統的內容放在 `rootfs/`，由 `cmake/make-rootfs.cmake` 在建置時打包成
映像並輸出為 C++ 位元組陣列，連結進核心，開機時透過 `io::RamDisk` 掛載。
因此在還沒有磁碟驅動程式之前就有檔案系統可用。檔案系統只透過
`io::BlockDevice` 存取資料，讀取一律經過一個區塊大小的緩衝區，換成真正的
磁碟驅動程式時同一份程式碼可以直接沿用。主機建置連結的是同一份產生的映像，
`tests/file_system_smoke.cpp` 測的因此就是真正會開機的那個映像。

The root file system's content lives in `rootfs/`. At build time
`cmake/make-rootfs.cmake` packs it into an image, emits it as a C++ byte array
that is linked into the kernel, and boot mounts it through an `io::RamDisk`, so
there is a file system before any disk driver exists. The file system reaches
its data only through `io::BlockDevice` and always reads through one
block-sized buffer, so the same code carries over unchanged to a real disk
driver. The host build links the same generated image, which is what makes
`tests/file_system_smoke.cpp` a test of the image that actually boots.

路徑由 `lookup()` 逐段走訪，`.` 與 `..` 在走訪過程中處理，不先改寫字串；以
`/` 開頭的路徑從根目錄開始，其他路徑從呼叫端給的 base 項目開始，shell 的
相對路徑就是這樣解析的。名稱上限 55 個位元組，路徑上限 128 個位元組，巢狀
深度上限 16 層，超過都是明確的失敗而不是截斷。

`lookup()` walks a path component by component, handling `.` and `..` as it
goes rather than rewriting the string first. A path starting with `/` is walked
from the root and any other from the base entry the caller supplies, which is
how the shell resolves a relative path. A name is at most 55 bytes, a path 128
bytes, and nesting 16 levels deep; passing any of those is an explicit failure
rather than a truncation.

## 主控台 Shell / The console shell

`shirley::shell::run()` 是核心啟動流程的最後一步，不會返回：開機之後，機器
就是這個提示符。shell 跑在核心裡而不是 user 行程裡，因為行程還沒有辦法結束
後把控制權交回給啟動它的人；`hello` 指令會先說明這件事，再把 CPU 交給嵌在
核心裡的 user 程式。

`shirley::shell::run()` is the last step of kernel start-up and never returns:
after boot, the machine is this prompt. The shell runs inside the kernel rather
than as a user process because a process cannot yet exit back to whatever
started it; the `hello` command says so before handing the CPU to the user
program embedded in the kernel.

行編輯器從標準輸入取字元，佇列空的時候停在等待中斷的低功耗狀態，不輪詢任何
裝置。按鍵可能剛好落在檢查與等待之間，那一次會等到下一個計時器中斷才醒來，
最多 10 毫秒——為了省下這點延遲去輪詢鍵盤並不划算。回顯屬於行編輯器而不是
驅動程式：空行時的 Backspace 必須什麼都不做，否則會把提示符擦掉。換行與回車
都算送出一行，因為鍵盤驅動程式解出的是換行，序列終端機送的是回車。

The line editor takes characters from standard input and, when the queue is
empty, parks in a low-power wait for the next interrupt rather than polling
anything. A keystroke can land between the check and the wait, in which case
that pass sleeps until the next timer interrupt — at most 10 ms, which is not
worth polling a keyboard to avoid. Echo belongs to the line editor rather than
to a driver: a backspace on an empty line must do nothing, or it would erase
the prompt. Both newline and carriage return end a line, because the keyboard
driver decodes one and a serial terminal sends the other.

指令有 `help`、`ls`、`cat`、`cd`、`pwd`、`stat`、`echo`、`mem`、`uptime`、
`version`、`clear`、`hello`、`reboot` 與 `poweroff`。`cd` 儲存的是由項目本身
組回來的絕對路徑，因此提示符顯示的永遠是正規化過的路徑，而不是使用者打進來
的那串 `../..`。沒有輸入裝置的平台（例如 apple_silicon）不會顯示提示符：
那只會騙人，因為永遠不會有字元進來。

The commands are `help`, `ls`, `cat`, `cd`, `pwd`, `stat`, `echo`, `mem`,
`uptime`, `version`, `clear`, `hello`, `reboot`, and `poweroff`. `cd` stores
the absolute path rebuilt from the entry itself, so the prompt always shows a
normalized path rather than the `../..` that was typed. A platform with no
input device, such as apple_silicon, gets no prompt: it would only lie, because
no character can ever arrive.

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
  進入長模式，然後以 `RDI` 帶著記憶體地圖位址跳進核心。核心分四次 BIOS 讀取
  載入，每次 64 個磁區共 128 KiB：單次讀取的磁區數有上限，緩衝區也不能跨越
  64 KiB 邊界。CMake 的 `--pad-to` 與映像大小檢查必須跟 `boot.S` 的
  `sectors_per_read` 與 `kernel_reads` 一致，否則核心會被靜默截斷。
* Apple Silicon 預期 `x0` 指向 Apple 的 `boot_args` 結構，與 iBoot 和 m1n1
  使用的契約相同。

* ARM64 uses QEMU `virt -kernel`. The firmware passes a flattened device tree
  in `x0`; `platform/qemu_arm64` reads its `/memory` node and reservation
  block.
* x86_64 uses a 512-byte BIOS boot sector in `arch/x86_64/boot.S`. It collects
  the E820 memory map, reads the kernel, identity-maps the first 1 GiB with
  2 MiB pages, enters long mode, and jumps to the kernel with the memory map
  address in `RDI`. The kernel arrives in four BIOS reads of 64 sectors each,
  128 KiB in total: a single read can only move so many sectors and its buffer
  must not cross a 64 KiB boundary. CMake's `--pad-to` and image size check
  must agree with `sectors_per_read` and `kernel_reads` in `boot.S`, or the
  kernel is silently truncated.
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

中斷子系統也在本里程碑接通，兩種架構都是。x86_64 的 IDT 之後接上 8259A
啟用用後端、通用 IRQ 層、PIT 計時器與 PS/2 鍵盤，因此有一條完整的中斷驅動
輸入路徑，閒置時停在 `hlt` 而不是輪詢裝置。ARM64 在同一個 `shirley::irq`
介面之下接上 GICv2 與架構計時器（`platform/arm/`），Apple Silicon 則接上
自家的 AIC。詳見「中斷子系統」一節。

The interrupt subsystem is joined up in this milestone too, on both
architectures. Behind x86_64's IDT sit the 8259A bring-up backend, the generic
IRQ layer, the PIT timer, and the PS/2 keyboard, giving it a complete
interrupt-driven input path that idles in `hlt` instead of polling a device.
ARM64 gets a GICv2 and the architected timer in `platform/arm/` behind the
same `shirley::irq` interface, and Apple Silicon gets its own AIC. See the
interrupt subsystem section.

本里程碑尚未完成的部分：ARM64 的 MMU 已經實作（`arch::arm64::mmu_enable`）
但開機時尚未啟用；ARM64 也還沒有輸入裝置驅動程式，因此中斷驅動的鍵盤目前
只有 x86_64 有。Apple Silicon 的 AIC 路徑雖然完整，卻從未實際執行過 ——
QEMU 沒有對應的機器模型 —— 而且它的架構計時器要等 M8 解析 Apple 裝置樹之後
才能啟用。UEFI 載入器面對韌體的那一段同樣只經過審閱，尚未實際開機驗證；
可以在沒有韌體的情況下測試的部分（ELF 讀取、記憶體地圖轉換、交接驗證）
則已由 `tests/boot_loader_smoke.cpp` 涵蓋。

Not yet done in this milestone: the ARM64 MMU is implemented
(`arch::arm64::mmu_enable`) but not enabled at boot, and ARM64 has no input
device driver, so the interrupt-driven keyboard exists only on x86_64. Apple
Silicon's AIC path is complete but has never executed, because QEMU has no
matching machine model, and its architected timer waits on the Apple device
tree parsing in M8. The firmware-facing part of the UEFI loader has likewise
been reviewed but not yet booted; the parts that can be tested without
firmware — ELF reading, memory map conversion, handoff validation — are
covered by `tests/boot_loader_smoke.cpp`.

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

### M1.5 — 檔案系統與主控台 / file system and console

開機的終點是一個可用的主控台。核心掛上唯讀的 SHRFS1 根檔案系統，然後進入
核心內的 shell：`ls`、`cat`、`cd` 走的是真正掛載起來的檔案系統，每個字元都
由中斷送達，沒有輸入時 CPU 停在等待中斷的狀態。x86_64 有 PS/2 鍵盤與序列埠
兩個輸入裝置，ARM64 有 PL011 的接收路徑，字元都進入同一個共用佇列。

Boot ends at a usable console. The kernel mounts the read-only SHRFS1 root file
system and enters an in-kernel shell: `ls`, `cat`, and `cd` walk a file system
that is really mounted, every character arrives through an interrupt, and with
nothing to read the CPU waits for one. x86_64 has two input devices, the PS/2
keyboard and the serial port; ARM64 has the PL011's receive path; and all of
them feed the same shared queue.

尚未完成的是寫入、每個指令一個行程（因此 `hello` 之後回不到提示符），以及
真正的磁碟驅動程式——根檔案系統目前住在核心映像裡。

Still missing: write support, a process per command (which is why the prompt
does not come back after `hello`), and a real disk driver — the root file
system lives inside the kernel image for now.

### 後續規劃 / Roadmap

M2 搶佔式排程與執行緒（兩種架構的中斷與計時器都已在 M0.5 完成）；M3 行程與
IPC，讓指令能以 user 行程執行並回到提示符；M4 完整 VFS：可寫入的檔案系統、
多個掛載點，以及跑在 user 空間的 shell；M5 PCI/VirtIO、儲存與 MSI/MSI-X；
M6 網路；M7 實體 x86_64 PC 啟用與 APIC/IOAPIC；M8 Apple Silicon 啟用；
M9 GUI 與視窗系統。

M2 preemptive scheduling and threads (interrupts and a timer landed on both
architectures in M0.5); M3 processes and IPC, so a command can run as a user
process and return to the prompt; M4 a full VFS: writable file systems, several
mount points, and a shell that runs in user space; M5 PCI/VirtIO, storage, and
MSI/MSI-X; M6 networking; M7 physical x86_64 PC bring-up and APIC/IOAPIC;
M8 Apple Silicon bring-up; M9 GUI/window system.

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
