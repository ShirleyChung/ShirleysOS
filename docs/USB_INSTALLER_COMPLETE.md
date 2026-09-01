# ShirleyOS USB 可開機安裝程式 - 實作完成
# ShirleyOS Bootable USB Installer - Implementation Complete

## 概述 / Overview

已為 ShirleyOS 實作完整的 USB 可開機安裝程式系統，支援在實體硬體上開機和運行。

A complete bootable USB installer system has been implemented for ShirleyOS, supporting booting and running on physical hardware.

## 已完成的組件 / Completed Components

### 1. USB 映像建立工具 / USB Image Creator Tool
📄 `tools/make-usb-installer.sh`

功能 / Features:
- ✅ 支援 x86_64 和 ARM64 架構
- ✅ 自動建置 ShirleyOS 核心
- ✅ 建立 UEFI 可開機 GPT 映像
- ✅ macOS 和 Linux 雙平台支援
- ✅ FAT32 EFI 系統分割區
- ✅ 自動複製開機載入器和核心
- ✅ 包含完整根檔案系統

### 2. 測試工具 / Testing Tool
📄 `tools/test-usb-installer.sh`

功能 / Features:
- ✅ 驗證所有腳本和文件
- ✅ 檢查必要工具
- ✅ 測試腳本執行

### 3. 文件 / Documentation

#### 完整說明文件
📄 `docs/USB_INSTALLER.md`
- 詳細的使用說明
- 疑難排解指南
- 技術細節
- 安全性注意事項

#### 快速參考
📄 `docs/USB_QUICKREF.md`
- 常用指令快速參考
- 一頁式說明

#### USB 內說明文件
📄 `rootfs/INSTALL_README.txt`
- USB 裝置上的完整使用說明
- 開機後指引
- 疑難排解

#### 主文件更新
📄 `README.md`
- 新增 USB 安裝程式章節
- 快速開始指引

## 使用方式 / Usage

### 建立 USB 映像 / Create USB Image

```bash
# x86_64
./tools/make-usb-installer.sh --arch x86_64

# ARM64
./tools/make-usb-installer.sh --arch arm64
```

### 寫入 USB / Write to USB

#### macOS
```bash
diskutil list
diskutil unmountDisk /dev/diskN
sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/rdiskN bs=1m
diskutil eject /dev/diskN
```

#### Linux
```bash
lsblk
sudo umount /dev/sdX*
sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/sdX bs=4M status=progress && sync
sudo eject /dev/sdX
```

### 從 USB 開機 / Boot from USB

1. 插入 USB 到目標電腦
2. 按 F2/F12/DEL/ESC 進入開機選單
3. 選擇 USB 裝置
4. ShirleyOS 啟動

## 技術實作 / Technical Implementation

### 映像結構 / Image Structure

```
USB Image (GPT, FAT32)
├── EFI/
│   └── BOOT/
│       ├── BOOTX64.EFI      (x86_64 UEFI Bootloader)
│       └── BOOTAA64.EFI     (ARM64 UEFI Bootloader)
├── shirley/
│   ├── kernel.elf           (ShirleyOS Kernel)
│   └── rootfs/              (Root Filesystem)
│       ├── bin/
│       ├── docs/
│       ├── etc/
│       └── home/
└── README.txt               (Installation Guide)
```

### 開機流程 / Boot Process

```
UEFI Firmware
    ↓
Read GPT Partition Table
    ↓
Find EFI System Partition
    ↓
Load EFI/BOOT/BOOTX64.EFI (or BOOTAA64.EFI)
    ↓
ShirleyOS UEFI Bootloader
    ↓
Read shirley/kernel.elf
    ↓
Setup Memory Mapping
    ↓
Call ExitBootServices()
    ↓
Jump to Kernel Entry Point
    ↓
ShirleyOS Kernel Boots
    ↓
Mount Root Filesystem
    ↓
Start Shell
```

### 支援的硬體 / Supported Hardware

**x86_64:**
- UEFI 韌體
- 512MB+ RAM
- PS/2 或 USB 鍵盤
- 序列埠（可選）

**ARM64:**
- UEFI 韌體（EDK2/AAVMF）
- 512MB+ RAM
- 序列埠輸入

### BIOS/UEFI 需求 / BIOS/UEFI Requirements

必須設定 / Required:
- ✅ UEFI 模式（非 Legacy/CSM）
- ✅ 停用 Secure Boot

建議設定 / Recommended:
- USB 開機優先權
- 快速開機停用（較詳細的訊息）

## 測試驗證 / Testing Verification

### 已測試環境 / Tested Environments

✅ **macOS (Apple Silicon)**
- hdiutil 映像建立
- 腳本執行
- 文件驗證

⏳ **QEMU Emulation**
- 可透過 QEMU 測試映像
- 不需實體 USB

⏳ **Linux**
- 腳本支援 parted/losetup
- 需要 root 權限

⏳ **實體硬體 / Physical Hardware**
- 等待實體機器測試
- x86_64 UEFI PC
- ARM64 UEFI 系統

### 運行測試 / Run Tests

```bash
./tools/test-usb-installer.sh
```

輸出 / Output:
```
[Test] 測試 USB 安裝程式建立工具
[Test] ✓ make-usb-installer.sh 存在
[Test] ✓ make-usb-installer.sh 可執行
[Test] ✓ --help 選項正常
[Test] ✓ USB_INSTALLER.md 存在
[Test] ✓ INSTALL_README.txt 存在
[Test] ✓ hdiutil 可用 (macOS)
[Test] 所有測試通過！
```

## 檔案清單 / File List

```
tools/
├── make-usb-installer.sh          主要建立工具
└── test-usb-installer.sh          測試腳本

docs/
├── USB_INSTALLER.md               完整說明文件
└── USB_QUICKREF.md                快速參考

rootfs/
└── INSTALL_README.txt             USB 內說明

README.md                          主文件（已更新）
```

## 特色功能 / Key Features

### 自動化建置 / Automated Build
- 自動偵測並建置目標架構
- 驗證建置產物
- 錯誤檢查和報告

### 跨平台支援 / Cross-Platform Support
- macOS: 使用 hdiutil
- Linux: 使用 parted/losetup
- 統一的使用者介面

### 完整文件 / Complete Documentation
- 中英雙語
- 多層次說明（完整、快速、USB 內）
- 疑難排解指南

### 安全性 / Safety
- 確認提示避免意外
- 清楚的警告訊息
- 裝置驗證

## 未來改進 / Future Improvements

計劃中的功能 / Planned:

- [ ] 互動式磁碟安裝程式
- [ ] 自動硬體偵測
- [ ] 網路安裝選項
- [ ] 多重開機支援
- [ ] Secure Boot 簽章
- [ ] GUI 安裝介面
- [ ] 驅動程式選擇
- [ ] 分割區工具

## 開發者注意事項 / Developer Notes

### 建置需求 / Build Requirements

macOS:
```bash
brew install cmake ninja llvm lld qemu
```

Linux:
```bash
sudo apt-get install cmake ninja-build clang lld qemu-system \
    parted dosfstools e2fsprogs
```

### 映像大小 / Image Size

預設: 512 MB
可調整: `--size` 選項

### 測試映像 / Testing Image

不需要實體 USB：
```bash
qemu-system-x86_64 -bios /path/to/OVMF.fd \
    -drive format=raw,file=usb-installer/shirleyos-x86_64-installer.img \
    -m 512M -serial stdio
```

### 除錯 / Debugging

1. 檢查建置產物：
```bash
ls -l build-x86_64_uefi/kernel/shirley.elf
ls -l build-x86_64_uefi/boot/uefi/BOOTX64.EFI
```

2. 檢查映像內容（macOS）：
```bash
hdiutil attach usb-installer/shirleyos-x86_64-installer.img
ls -R /Volumes/SHIRLEY_USB/
hdiutil detach /Volumes/SHIRLEY_USB
```

3. 檢查映像內容（Linux）：
```bash
sudo losetup -f --show -P usb-installer/shirleyos-x86_64-installer.img
sudo mount /dev/loopXp1 /mnt
ls -R /mnt
sudo umount /mnt
sudo losetup -d /dev/loopX
```

## 貢獻指南 / Contributing

如需改進 USB 安裝程式：

1. 測試在不同硬體上的相容性
2. 回報開機問題和錯誤
3. 改進文件和說明
4. 增加新功能（見未來改進）

## 授權 / License

遵循 ShirleyOS 專案授權

Follow ShirleyOS project license

## 聯絡方式 / Contact

問題回報：GitHub Issues
文件改進：Pull Requests

---

實作完成日期 / Implementation Date: 2026-09-01
版本 / Version: 1.0
狀態 / Status: ✅ 完成 / Complete
