.intel_syntax noprefix

# 開機磁區的固定記憶體配置（實體位址）：
#   0x7c00  本磁區
#   0x8000  韌體記憶體地圖：uint32 項目數、uint32 保留、之後為 24 位元組的 E820 項目
#   0x9000  PML4
#   0xa000  PDPT
#   0xb000  PD（512 個 2 MiB 分頁，涵蓋前 1 GiB）
#   0x10000 核心映像
#
# The boot sector's fixed physical memory layout:
#   0x7c00  this sector
#   0x8000  firmware memory map: uint32 count, uint32 reserved, 24-byte E820 entries
#   0x9000  PML4
#   0xa000  PDPT
#   0xb000  PD (512 x 2 MiB pages, covering the first 1 GiB)
#   0x10000 kernel image

# 每次讀 64 個磁區（32 KiB），共讀 kernel_reads 次，也就是 128 KiB 的核心
# 映像。CMake 的 --pad-to 與映像大小檢查必須與這兩個常數一致，否則核心會被
# 靜默截斷。每一段都落在 32 KiB 對齊的位址上，因此不可能跨越 64 KiB 邊界。
#
# Each read moves 64 sectors (32 KiB), and there are kernel_reads of them, so
# 128 KiB of kernel image in total. CMake's --pad-to and image size check must
# agree with these two constants or the kernel is silently truncated. Every
# chunk lands on a 32 KiB aligned address, so none can cross a 64 KiB boundary.
.equ sectors_per_read, 64
.equ kernel_reads, 4
.equ paragraphs_per_read, sectors_per_read * 512 / 16

.section .text
.code16
.global _start
_start:
    # 關閉中斷並建立實模式下的資料段與堆疊。
    # Disable interrupts and set up real-mode data segments and a stack.
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    # E820 查詢會蓋掉 dx，先保存 BIOS 傳入的開機磁碟編號。
    # The E820 query clobbers dx, so save the boot drive BIOS passed in dl.
    mov [boot_drive], dl

    # 透過 BIOS INT 15h E820 取得實體記憶體地圖。
    # Collect the physical memory map through BIOS INT 15h, EAX=E820.
    mov dword ptr [0x8000], 0
    mov dword ptr [0x8004], 0
    mov di, 0x8008
    xor ebx, ebx
    xor bp, bp
e820_next:
    mov eax, 0xe820
    mov edx, 0x534d4150
    mov ecx, 24
    # ACPI 3.0 的擴充屬性欄位預設標記為有效。
    # Default the ACPI 3.0 extended attribute field to "valid".
    mov dword ptr [di + 20], 1
    int 0x15
    jc e820_done
    cmp eax, 0x534d4150
    jne e820_done
    # 略過長度為零的項目。
    # Skip zero-length entries.
    mov eax, [di + 8]
    or eax, [di + 12]
    jz e820_skip
    inc bp
    add di, 24
e820_skip:
    test ebx, ebx
    jz e820_done
    cmp bp, 64
    jb e820_next
e820_done:
    mov [0x8000], bp
    mov cx, kernel_reads

    # 透過 BIOS 擴充磁碟服務讀取核心。單次 BIOS 讀取能搬的磁區數有限，
    # 而且緩衝區不可以跨越 64 KiB 邊界，因此核心分成數次固定大小的讀取
    # 載入；每讀完一段就把目的地段位址與 LBA 一起往前推。
    #
    # Read the kernel with the BIOS extended disk service. A single BIOS read
    # can only move so many sectors and its buffer must not cross a 64 KiB
    # boundary, so the kernel arrives in several fixed-size reads, advancing
    # both the destination segment and the LBA after each one.
read_kernel:
    # int 13h 可能改動 cx 與 dl，因此每一圈都重新設定，圈數自行保存。
    # int 13h may clobber cx and dl, so both are set again on every pass and
    # the loop counter is preserved by hand.
    push cx
    mov dl, [boot_drive]
    mov ah, 0x42
    mov si, offset dap
    int 0x13
    jc disk_error
    add word ptr [dap_segment], paragraphs_per_read
    add dword ptr [dap_lba], sectors_per_read
    pop cx
    loop read_kernel

    # 開啟 A20 line。
    # Open the A20 line.
    in al, 0x92
    or al, 2
    out 0x92, al

    # 清除分頁表區域，避免殘留資料被當成有效項目。
    # Clear the page table area so leftover data cannot look like a valid entry.
    mov di, 0x9000
    xor eax, eax
    mov cx, 0xc00
    rep stosd
    # PML4[0] -> PDPT，PDPT[0] -> PD。
    # PML4[0] points at the PDPT, PDPT[0] points at the PD.
    mov dword ptr [0x9000], 0xa003
    mov dword ptr [0xa000], 0xb003
    # PD 的 512 個項目各對應一個 2 MiB 分頁，合計 identity map 前 1 GiB。
    # The PD's 512 entries each map a 2 MiB page, identity-mapping 1 GiB.
    mov di, 0xb000
    mov eax, 0x83
    mov cx, 512
fill_page_directory:
    mov [di], eax
    add eax, 0x200000
    add di, 8
    loop fill_page_directory

    # 載入 GDT，準備進入 64 位元長模式。
    # Load the GDT and prepare to enter 64-bit long mode.
    lgdt [gdt_descriptor]
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, 0x9000
    mov cr3, eax
    mov ecx, 0xc0000080
    rdmsr
    or eax, 0x100
    wrmsr
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    # 以遠跳轉切換至 64 位元程式碼段。
    # Far jump into the 64-bit code segment.
    ljmp 0x08, offset long_mode
disk_error: hlt
    jmp disk_error

.align 8
# DAP：從 LBA 1 開始，每次把 sectors_per_read 個磁區讀到 0x1000:0 之後。
# Disk address packet: read sectors_per_read sectors at a time, starting at
# LBA 1 and landing at 0x1000:0 onwards.
dap: .byte 0x10, 0
    .word sectors_per_read
    .word 0
dap_segment: .word 0x1000
dap_lba: .quad 1
boot_drive: .byte 0

.code64
long_mode:
    # 設定長模式資料段與暫時堆疊，並把韌體記憶體地圖位址傳給核心。
    # Set up long-mode data segments and a temporary stack, then hand the
    # firmware memory map address to the kernel in RDI.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, 0x80000
    mov edi, 0x8000
    mov eax, 0x10000
    jmp rax

.align 8
gdt:
    # 空描述元、核心程式碼段與核心資料段。
    # The null descriptor, the kernel code segment, and the kernel data segment.
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdt_descriptor: .word 23
    .quad gdt
.org 510
.word 0xaa55
