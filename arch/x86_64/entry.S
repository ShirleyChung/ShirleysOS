.intel_syntax noprefix

# 核心映像的第一個位元組。連結腳本把 .text.boot 排在最前面，
# 開機載入器因此可以直接跳到核心載入位址。
#
# The first byte of the kernel image. The linker script places .text.boot
# first, so the boot loader can jump straight to the load address.
.section .text.boot
.global _start
_start:
    # BIOS 路徑進來時中斷已經關閉，但 UEFI 韌體會把中斷開著，
    # 而此時 IDT 還是韌體的，因此一律先關中斷。
    # Interrupts are already off when arriving from the BIOS path, but UEFI
    # firmware leaves them enabled while the IDT still belongs to the firmware,
    # so disable them unconditionally.
    cli
    # rdi 由開機載入器帶入韌體資料位址，先移到不會被 rep stosb 破壞的暫存器。
    # The boot loader passes the firmware data address in rdi; park it in a
    # register rep stosb will not clobber.
    mov r15, rdi
    lea rsp, [rip + boot_stack_top]
    xor ebp, ebp
    # objcopy 產生的純二進位映像不含 .bss 內容，必須自行清零。
    # A flat binary produced by objcopy carries no .bss contents, so zero it here.
    lea rdi, [rip + __bss_start]
    lea rcx, [rip + __bss_end]
    sub rcx, rdi
    xor eax, eax
    cld
    rep stosb
    # 由平台層把韌體資料轉換成通用 BootInfo，再進入核心。
    # Let the platform layer turn the firmware data into a neutral BootInfo,
    # then enter the kernel.
    mov rdi, r15
    call shirley_platform_boot_info
    mov rdi, rax
    call kernel_main
1:  cli
    hlt
    jmp 1b

.section .bss
.align 16
boot_stack:
    .skip 32768
.global boot_stack_top
boot_stack_top:
