# ShirleyOS USB 安裝程式快速參考 / Quick Reference

## 建立 USB 安裝程式 / Create USB Installer

### 基本用法 / Basic Usage

```bash
# x86_64 架構
./tools/make-usb-installer.sh --arch x86_64

# ARM64 架構  
./tools/make-usb-installer.sh --arch arm64
```

### 輸出 / Output

映像檔位置：
- `usb-installer/shirleyos-x86_64-installer.img`
- `usb-installer/shirleyos-arm64-installer.img`

## 寫入 USB / Write to USB

### macOS

```bash
# 1. 找出 USB 裝置
diskutil list

# 2. 卸載但不退出
diskutil unmountDisk /dev/diskN

# 3. 寫入映像（rdiskN 更快）
sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/rdiskN bs=1m

# 4. 退出
diskutil eject /dev/diskN
```

### Linux

```bash
# 1. 找出 USB 裝置
lsblk

# 2. 卸載分割區
sudo umount /dev/sdX*

# 3. 寫入映像
sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/sdX bs=4M status=progress && sync

# 4. 退出
sudo eject /dev/sdX
```

## 開機 / Boot

1. 插入 USB
2. 按 F2/F12/DEL/ESC 進入開機選單
3. 選擇 USB 裝置
4. ShirleyOS 啟動

## BIOS 設定 / BIOS Settings

必須設定：
- ✅ UEFI 模式（非 Legacy）
- ✅ 停用 Secure Boot

## Shell 指令 / Shell Commands

```
help        - 顯示指令
ls          - 列出檔案
cat FILE    - 顯示檔案
cd DIR      - 切換目錄
pwd         - 當前目錄
devices     - 列出裝置
mem         - 記憶體資訊
uptime      - 運行時間
version     - 版本資訊
hello       - 測試程式
reboot      - 重新啟動
poweroff    - 關機
```

## 疑難排解 / Troubleshooting

### USB 無法開機
- 檢查 UEFI 模式
- 停用 Secure Boot
- 試不同 USB 埠
- 確認映像寫入成功

### 鍵盤無反應
- 試 PS/2 鍵盤
- 連接序列埠 (115200 8N1)
- 在 QEMU 測試

### 無提示符
- 等待幾秒
- 按 Enter
- 檢查序列輸出

## 測試在 QEMU / Test in QEMU

```bash
# x86_64
qemu-system-x86_64 -bios /usr/share/qemu/OVMF.fd \
    -drive format=raw,file=usb-installer/shirleyos-x86_64-installer.img \
    -m 512M -serial stdio

# ARM64
qemu-system-aarch64 -bios /usr/share/qemu/AAVMF.fd \
    -drive format=raw,file=usb-installer/shirleyos-arm64-installer.img \
    -m 512M -serial stdio -M virt -cpu cortex-a57
```

## 檔案結構 / File Structure

```
USB/
├── EFI/BOOT/
│   └── BOOTX64.EFI (or BOOTAA64.EFI)
├── shirley/
│   ├── kernel.elf
│   └── rootfs/
└── README.txt
```

## 工具腳本 / Tool Scripts

- `tools/make-usb-installer.sh` - 建立 USB 映像
- `tools/test-usb-installer.sh` - 測試工具
- `docs/USB_INSTALLER.md` - 完整文件
- `rootfs/INSTALL_README.txt` - USB 上的說明

---

更多資訊請參考：`docs/USB_INSTALLER.md`
