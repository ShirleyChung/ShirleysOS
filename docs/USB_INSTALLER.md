# ShirleyOS USB 可開機安裝程式 / ShirleyOS Bootable USB Installer

本文件說明如何建立 ShirleyOS 的 USB 可開機安裝程式。

This document explains how to create a bootable USB installer for ShirleyOS.

## 功能特色 / Features

- ✅ UEFI 開機支援 / UEFI boot support
- ✅ x86_64 和 ARM64 架構 / x86_64 and ARM64 architectures  
- ✅ GPT 分割表 / GPT partition table
- ✅ FAT32 EFI 系統分割區 / FAT32 EFI System Partition
- ✅ 完整的根檔案系統 / Complete root filesystem
- ✅ 自動化建置流程 / Automated build process

## 系統需求 / Requirements

### macOS
```bash
brew install cmake ninja llvm lld qemu
```

### Linux
```bash
sudo apt-get install cmake ninja-build clang lld qemu-system parted dosfstools
```

## 快速開始 / Quick Start

### 1. 建立 USB 映像檔 / Create USB Image

```bash
# x86_64 架構
./tools/make-usb-installer.sh --arch x86_64

# ARM64 架構
./tools/make-usb-installer.sh --arch arm64
```

映像檔將被建立在 `usb-installer/shirleyos-{arch}-installer.img`

The image will be created at `usb-installer/shirleyos-{arch}-installer.img`

### 2. 寫入 USB 裝置 / Write to USB Device

#### macOS

```bash
# 1. 找出 USB 裝置編號
diskutil list

# 2. 卸載 USB（但不要退出）
diskutil unmountDisk /dev/diskN

# 3. 寫入映像（使用 rdiskN 更快）
sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/rdiskN bs=1m

# 4. 完成後退出
diskutil eject /dev/diskN
```

#### Linux

```bash
# 1. 找出 USB 裝置
lsblk

# 2. 卸載 USB 所有分割區
sudo umount /dev/sdX*

# 3. 寫入映像
sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/sdX bs=4M status=progress && sync

# 4. 完成後安全移除
sudo eject /dev/sdX
```

⚠️ **警告 / Warning**: 請確認裝置編號正確，dd 指令會清除目標裝置上的所有資料！

⚠️ **Warning**: Make sure the device number is correct - dd will erase all data on the target device!

### 3. 從 USB 開機 / Boot from USB

1. 將 USB 插入目標電腦 / Insert USB into target computer
2. 開機時進入 BIOS/UEFI 設定 / Enter BIOS/UEFI settings during boot
   - 常見按鍵：F2, F12, DEL, ESC / Common keys: F2, F12, DEL, ESC
3. 選擇 USB 裝置開機 / Select USB device to boot
4. ShirleyOS 將自動啟動 / ShirleyOS will boot automatically

## USB 映像結構 / USB Image Structure

```
USB (FAT32, GPT)
├── EFI/
│   └── BOOT/
│       └── BOOTX64.EFI (or BOOTAA64.EFI)  ← UEFI 開機載入器
├── shirley/
│   ├── kernel.elf                         ← ShirleyOS 核心
│   └── rootfs/                            ← 根檔案系統
│       ├── README.md
│       ├── docs/
│       ├── etc/
│       ├── home/
│       └── bin/
└── README.txt                             ← 說明文件
```

## 進階選項 / Advanced Options

### 自訂映像大小 / Custom Image Size

```bash
./tools/make-usb-installer.sh --arch x86_64 --size 1024
```

### 指定輸出位置 / Specify Output Location

```bash
./tools/make-usb-installer.sh --arch x86_64 \
    --output /path/to/custom-installer.img
```

### 查看所有選項 / View All Options

```bash
./tools/make-usb-installer.sh --help
```

## 開機流程 / Boot Process

```
韌體 (UEFI)
    ↓
EFI/BOOT/BOOTX64.EFI (ShirleyOS UEFI Loader)
    ↓
載入 shirley/kernel.elf
    ↓
掛載根檔案系統
    ↓
啟動 ShirleyOS Shell
```

1. **UEFI 韌體** 讀取 GPT 分割表並找到 EFI 系統分割區
   
   **UEFI Firmware** reads GPT partition table and finds EFI System Partition

2. **開機載入器** (`BOOTX64.EFI` 或 `BOOTAA64.EFI`) 被載入並執行
   
   **Bootloader** (`BOOTX64.EFI` or `BOOTAA64.EFI`) is loaded and executed

3. **載入器** 讀取 `shirley/kernel.elf` 並設置記憶體映射
   
   **Loader** reads `shirley/kernel.elf` and sets up memory mapping

4. **核心** 啟動並掛載根檔案系統
   
   **Kernel** boots and mounts root filesystem

5. **Shell** 提供互動式命令列介面
   
   **Shell** provides interactive command-line interface

## 測試 / Testing

### 在 QEMU 中測試 / Test in QEMU

不需要實體 USB 即可測試映像：

You can test the image without physical USB:

```bash
# x86_64
qemu-system-x86_64 -bios /path/to/OVMF.fd \
    -drive format=raw,file=usb-installer/shirleyos-x86_64-installer.img \
    -m 512M -serial stdio

# ARM64 (如果有 QEMU ARM64 UEFI 韌體)
qemu-system-aarch64 -bios /path/to/AAVMF.fd \
    -drive format=raw,file=usb-installer/shirleyos-arm64-installer.img \
    -m 512M -serial stdio -M virt -cpu cortex-a57
```

## 疑難排解 / Troubleshooting

### 映像建立失敗 / Image Creation Fails

```bash
# 檢查建置是否成功
./shirley build x86_64_uefi

# 檢查建置產物
ls -l build-x86_64_uefi/kernel/shirley.elf
ls -l build-x86_64_uefi/boot/uefi/BOOTX64.EFI
```

### USB 無法開機 / USB Won't Boot

1. 確認 BIOS/UEFI 設定中啟用了 UEFI 模式
   
   Confirm UEFI mode is enabled in BIOS/UEFI settings

2. 確認安全開機 (Secure Boot) 已停用
   
   Confirm Secure Boot is disabled

3. 嘗試其他 USB 連接埠
   
   Try different USB ports

4. 檢查 USB 是否正確寫入
   
   Check if USB was written correctly
   
   ```bash
   # macOS
   diskutil list
   
   # Linux
   sudo fdisk -l
   ```

### macOS hdiutil 錯誤 / macOS hdiutil Errors

如果遇到權限錯誤：

If you encounter permission errors:

```bash
# 賦予腳本完整磁碟存取權限
# Grant script full disk access
# 系統偏好設定 > 安全性與隱私 > 隱私 > 完整磁碟存取
# System Preferences > Security & Privacy > Privacy > Full Disk Access
```

## 檔案系統內容 / Filesystem Contents

USB 內的根檔案系統包含：

The root filesystem on USB contains:

- `/README.md` - 專案說明 / Project description
- `/docs/` - 文件 / Documentation  
- `/etc/` - 系統設定檔 / System configuration
  - `/etc/motd` - 歡迎訊息 / Welcome message
  - `/etc/version` - 版本資訊 / Version info
- `/home/` - 使用者目錄 / User directories
- `/bin/` - 執行檔 / Executables
  - `/bin/hello` - 測試程式 / Test program

## 安全性注意事項 / Security Notes

- USB 映像使用 FAT32 檔案系統，沒有檔案權限保護
  
  USB image uses FAT32 filesystem without file permission protection

- 開機載入器和核心未經過簽章，需要停用 Secure Boot
  
  Bootloader and kernel are not signed, Secure Boot must be disabled

- 這是開發用映像，不適合生產環境使用
  
  This is a development image, not suitable for production use

## 未來改進 / Future Improvements

計劃中的功能：

Planned features:

- [ ] 互動式安裝程式 / Interactive installer
- [ ] 磁碟分割工具 / Disk partitioning tool  
- [ ] 網路安裝支援 / Network installation support
- [ ] 多重開機支援 / Multi-boot support
- [ ] Secure Boot 簽章 / Secure Boot signing
- [ ] 自動硬體偵測 / Automatic hardware detection
- [ ] 驅動程式選擇 / Driver selection
- [ ] 客製化安裝選項 / Customized installation options

## 相關文件 / Related Documents

- [OS_SPEC.md](../OS_SPEC.md) - ShirleyOS 完整規格
- [README.md](../README.md) - 專案概覽
- [ROADMAP.md](../ROADMAP.md) - 開發路線圖

## 授權 / License

ShirleyOS 是開源專案。詳見專案根目錄的授權檔案。

ShirleyOS is an open source project. See the license file in the project root.

## 貢獻 / Contributing

歡迎提交問題回報和改進建議！

Issues and improvements are welcome!

---

建立時間 / Created: 2026-09-01  
最後更新 / Last Updated: 2026-09-01
