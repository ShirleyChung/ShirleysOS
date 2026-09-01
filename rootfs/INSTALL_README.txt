===============================================
ShirleyOS USB Bootable Installer
ShirleyOS USB 可開機安裝程式
===============================================

感謝您使用 ShirleyOS！
Thank you for using ShirleyOS!

這個 USB 包含一個可開機的 ShirleyOS 系統。
This USB contains a bootable ShirleyOS system.

===============================================
開機說明 / Boot Instructions
===============================================

1. 將此 USB 插入您的電腦
   Insert this USB into your computer

2. 重新啟動電腦
   Restart your computer

3. 開機時按 F2, F12, DEL 或 ESC 進入開機選單
   Press F2, F12, DEL, or ESC during boot to enter boot menu

4. 選擇此 USB 裝置開機
   Select this USB device to boot

5. ShirleyOS 將自動啟動
   ShirleyOS will start automatically

===============================================
系統需求 / System Requirements
===============================================

最低需求 / Minimum:
- UEFI 韌體支援 / UEFI firmware support
- 512 MB RAM
- x86_64 或 ARM64 處理器 / x86_64 or ARM64 processor

建議需求 / Recommended:
- 1 GB RAM 或更多 / or more
- 序列埠或 PS/2 鍵盤 / Serial port or PS/2 keyboard

===============================================
重要提示 / Important Notes
===============================================

⚠️ 安全開機 (Secure Boot)
   請在 BIOS/UEFI 設定中停用 Secure Boot
   Please disable Secure Boot in BIOS/UEFI settings

⚠️ 開機模式
   請確認使用 UEFI 模式開機（非 Legacy/CSM）
   Please boot in UEFI mode (not Legacy/CSM)

⚠️ 鍵盤輸入
   如果鍵盤無法輸入，請嘗試序列埠終端機
   If keyboard doesn't work, try serial terminal

===============================================
USB 內容 / USB Contents
===============================================

/EFI/BOOT/BOOTX64.EFI
    UEFI 開機載入器（x86_64）
    UEFI bootloader (x86_64)

/EFI/BOOT/BOOTAA64.EFI  
    UEFI 開機載入器（ARM64）
    UEFI bootloader (ARM64)

/shirley/kernel.elf
    ShirleyOS 核心映像
    ShirleyOS kernel image

/shirley/rootfs/
    根檔案系統
    Root filesystem
    - /bin/     執行檔 / Executables
    - /docs/    文件 / Documentation
    - /etc/     設定檔 / Configuration
    - /home/    使用者目錄 / User directories

===============================================
開機後操作 / After Booting
===============================================

ShirleyOS 將啟動到一個互動式 shell 提示符：

ShirleyOS will boot to an interactive shell prompt:

    shirley:/$ 

您可以嘗試以下指令：
You can try these commands:

    help        顯示所有可用指令 / Show all commands
    ls          列出檔案 / List files
    cat FILE    顯示檔案內容 / Show file contents
    cd DIR      切換目錄 / Change directory
    pwd         顯示當前目錄 / Show current directory
    devices     列出裝置 / List devices
    mem         顯示記憶體資訊 / Show memory info
    uptime      顯示運行時間 / Show uptime
    version     顯示版本資訊 / Show version
    hello       執行測試程式 / Run test program
    reboot      重新啟動 / Reboot
    poweroff    關機 / Power off

範例 / Examples:

    shirley:/$ ls /
    shirley:/$ cat /etc/motd
    shirley:/$ cd /docs
    shirley:/docs$ ls
    shirley:/docs$ cat README.md
    shirley:/docs$ hello

===============================================
檔案系統導覽 / Filesystem Tour
===============================================

/
├── README.md           專案說明 / Project info
├── docs/               文件目錄 / Documentation
│   ├── README.md
│   └── ...
├── etc/                系統設定 / System config
│   ├── motd            歡迎訊息 / Welcome message
│   └── version         版本資訊 / Version info
├── home/               使用者目錄 / User home
├── bin/                執行檔 / Executables
│   └── hello           測試程式 / Test program
└── dev/                裝置檔案 / Device files
    ├── console         主控台 / Console
    ├── null            空裝置 / Null device
    ├── uart0           序列埠 / Serial port
    ├── kbd0            鍵盤 / Keyboard (x86_64)
    └── ram0            RAM 磁碟 / RAM disk

===============================================
疑難排解 / Troubleshooting
===============================================

問題: USB 無法開機
Problem: USB won't boot

解決方法:
Solution:
1. 確認 BIOS/UEFI 使用 UEFI 模式
   Confirm BIOS/UEFI is in UEFI mode
2. 停用 Secure Boot
   Disable Secure Boot  
3. 嘗試不同的 USB 連接埠
   Try different USB ports
4. 確認 USB 正確寫入
   Verify USB was written correctly

---

問題: 鍵盤無法輸入
Problem: Keyboard doesn't work

解決方法:
Solution:
1. 嘗試不同的鍵盤
   Try a different keyboard
2. 使用 PS/2 鍵盤而非 USB 鍵盤
   Use PS/2 keyboard instead of USB
3. 連接序列埠終端機（115200 8N1）
   Connect serial terminal (115200 8N1)
4. 在 QEMU 中測試
   Test in QEMU

---

問題: 開機後看不到提示符
Problem: No prompt after boot

解決方法:
Solution:
1. 等待更長時間（某些系統需要幾秒鐘）
   Wait longer (some systems need a few seconds)
2. 按 Enter 鍵
   Press Enter
3. 檢查序列輸出
   Check serial output
4. 確認記憶體充足（需要至少 512MB）
   Confirm sufficient memory (need at least 512MB)

===============================================
技術資訊 / Technical Information
===============================================

架構 / Architecture:
- CPU 架構抽象層 / CPU architecture abstraction
- 平台獨立設計 / Platform-independent design
- UEFI 開機協定 / UEFI boot protocol

支援的硬體 / Supported Hardware:
- x86_64: QEMU PC, 標準 PC / Standard PC
- ARM64: QEMU virt, (Apple Silicon 開發中)
- 中斷: 8259A PIC (x86_64), GICv2 (ARM64)
- 計時器: PIT (x86_64), Generic Timer (ARM64)
- 輸入: PS/2 鍵盤, 序列埠 / Serial port
- 輸出: VGA 文字模式, 序列埠 / Serial port

檔案系統 / Filesystem:
- SHRFS1 唯讀格式 / Read-only format
- VFS 虛擬檔案系統層 / Virtual filesystem layer
- devfs 裝置檔案系統 / Device filesystem

記憶體管理 / Memory Management:
- 分頁式記憶體 / Paged memory
- 核心與使用者空間分離 / Kernel/user separation
- ELF 程式載入器 / ELF program loader

系統呼叫 / System Calls:
- read, write, open, close, exit
- POSIX 相容介面 / POSIX-compatible interface

===============================================
更多資訊 / More Information
===============================================

專案網站 / Project Website:
https://github.com/yourusername/ShirleysOS

文件 / Documentation:
在 USB 上: /shirley/rootfs/docs/
On USB: /shirley/rootfs/docs/

問題回報 / Issue Tracker:
https://github.com/yourusername/ShirleysOS/issues

===============================================
授權資訊 / License Information
===============================================

ShirleyOS 是開源專案。
ShirleyOS is an open source project.

詳見專案的授權檔案。
See the license file in the project.

===============================================

版本 / Version: 0.5 "console"
建立日期 / Build Date: 2026-09-01

祝您使用愉快！
Enjoy using ShirleyOS!

===============================================
