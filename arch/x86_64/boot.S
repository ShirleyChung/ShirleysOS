.intel_syntax noprefix
.section .text
.code16
.global _start
_start:
    # 關閉中斷並建立實模式下的資料段與堆疊。
    cli
    xor ax, ax
    mov ds, ax
    mov ss, ax
    mov sp, 0x7c00
    mov ah, 0x42
    # 透過 BIOS 擴充磁碟服務讀取核心。
    mov si, offset dap
    int 0x13
    jc disk_error
    in al, 0x92
    or al, 2
    out 0x92, al
    # 載入 GDT，準備進入 64 位元長模式。
    lgdt [gdt_descriptor]
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov dword ptr [0x9000], 0xa003
    mov dword ptr [0xa000], 0xb003
    mov dword ptr [0xb000], 0x0083
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
    push 0x08
    push offset long_mode
    retf
disk_error: hlt
    jmp disk_error
.align 8
// DAP：從 LBA 1 讀取 127 個磁區至 0x1000:0。
dap: .byte 0x10, 0
    .word 127
    .word 0
    .word 0x1000
    .quad 1
.code64
long_mode:
    # 設定長模式資料段與堆疊，呼叫已載入的核心。
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov rsp, 0x80000
    mov rax, 0x10000
    call rax
halt: hlt
    jmp halt
.align 8
gdt:
    # 空描述元、核心程式碼段與核心資料段。
    .quad 0
    .quad 0x00af9a000000ffff
    .quad 0x00af92000000ffff
gdt_descriptor: .word 23
    .quad gdt
.org 510
.word 0xaa55
