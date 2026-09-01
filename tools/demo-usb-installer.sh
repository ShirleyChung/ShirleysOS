#!/bin/bash
# ShirleyOS USB Installer Demo
# USB 安裝程式演示腳本

set -e

GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

clear

echo -e "${BLUE}"
cat << 'BANNER'
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║     ShirleyOS USB Bootable Installer Demo                ║
║     ShirleyOS USB 可開機安裝程式演示                     ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝
BANNER
echo -e "${NC}"

echo ""
echo -e "${GREEN}此腳本將演示如何建立 ShirleyOS USB 安裝程式${NC}"
echo -e "${GREEN}This script demonstrates how to create a ShirleyOS USB installer${NC}"
echo ""

sleep 2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${BLUE}步驟 1: 檢查系統環境 / Step 1: Check System Environment${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "作業系統 / OS: $(uname -s)"
echo "架構 / Arch: $(uname -m)"
echo ""

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "✓ 偵測到 macOS / Detected macOS"
    if command -v hdiutil &> /dev/null; then
        echo "✓ hdiutil 可用 / hdiutil available"
    else
        echo "✗ 缺少 hdiutil / Missing hdiutil"
        exit 1
    fi
else
    echo "✓ 偵測到 Linux / Detected Linux"
    if command -v parted &> /dev/null; then
        echo "✓ parted 可用 / parted available"
    else
        echo "✗ 缺少 parted / Missing parted"
        exit 1
    fi
fi

echo ""
sleep 2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${BLUE}步驟 2: 可用的架構選項 / Step 2: Available Architecture Options${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "支援的架構 / Supported Architectures:"
echo ""
echo "  1. x86_64  - 64位元 Intel/AMD 處理器"
echo "             - 64-bit Intel/AMD processors"
echo "             - QEMU PC, 標準 PC / Standard PC"
echo ""
echo "  2. arm64   - 64位元 ARM 處理器"
echo "             - 64-bit ARM processors"
echo "             - QEMU virt, ARM 伺服器 / ARM servers"
echo ""

sleep 2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${BLUE}步驟 3: 建立命令範例 / Step 3: Creation Command Examples${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "建立 x86_64 映像 / Create x86_64 image:"
echo -e "${YELLOW}  ./tools/make-usb-installer.sh --arch x86_64${NC}"
echo ""

echo "建立 ARM64 映像 / Create ARM64 image:"
echo -e "${YELLOW}  ./tools/make-usb-installer.sh --arch arm64${NC}"
echo ""

echo "自訂大小 / Custom size:"
echo -e "${YELLOW}  ./tools/make-usb-installer.sh --arch x86_64 --size 1024${NC}"
echo ""

sleep 2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${BLUE}步驟 4: 寫入 USB 步驟 / Step 4: Write to USB Steps${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "macOS 寫入步驟:"
    echo ""
    echo "1. 找出 USB 裝置 / Find USB device:"
    echo -e "${YELLOW}   diskutil list${NC}"
    echo ""
    echo "2. 卸載 USB / Unmount USB:"
    echo -e "${YELLOW}   diskutil unmountDisk /dev/diskN${NC}"
    echo ""
    echo "3. 寫入映像 / Write image:"
    echo -e "${YELLOW}   sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/rdiskN bs=1m${NC}"
    echo ""
    echo "4. 退出 USB / Eject USB:"
    echo -e "${YELLOW}   diskutil eject /dev/diskN${NC}"
else
    echo "Linux 寫入步驟:"
    echo ""
    echo "1. 找出 USB 裝置 / Find USB device:"
    echo -e "${YELLOW}   lsblk${NC}"
    echo ""
    echo "2. 卸載分割區 / Unmount partitions:"
    echo -e "${YELLOW}   sudo umount /dev/sdX*${NC}"
    echo ""
    echo "3. 寫入映像 / Write image:"
    echo -e "${YELLOW}   sudo dd if=usb-installer/shirleyos-x86_64-installer.img of=/dev/sdX bs=4M status=progress && sync${NC}"
    echo ""
    echo "4. 退出 USB / Eject USB:"
    echo -e "${YELLOW}   sudo eject /dev/sdX${NC}"
fi

echo ""
sleep 2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${BLUE}步驟 5: 開機設定 / Step 5: Boot Configuration${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "必須設定 / Required Settings:"
echo ""
echo "  ✓ UEFI 模式（非 Legacy/CSM）"
echo "    UEFI mode (not Legacy/CSM)"
echo ""
echo "  ✓ 停用 Secure Boot"
echo "    Disable Secure Boot"
echo ""
echo "  ✓ USB 開機優先"
echo "    USB boot priority"
echo ""

sleep 2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${BLUE}步驟 6: 開機流程 / Step 6: Boot Process${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "1. 插入 USB → 2. 按 F2/F12/DEL/ESC → 3. 選擇 USB → 4. ShirleyOS 啟動"
echo "   Insert USB → Press F2/F12/DEL/ESC → Select USB → ShirleyOS Boots"
echo ""

sleep 2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${BLUE}可用的文件 / Available Documentation${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "📄 docs/USB_INSTALLER.md         - 完整說明文件"
echo "                                   Complete documentation"
echo ""
echo "📄 docs/USB_QUICKREF.md          - 快速參考"
echo "                                   Quick reference"
echo ""
echo "📄 docs/USB_INSTALLER_COMPLETE.md - 實作說明"
echo "                                    Implementation notes"
echo ""
echo "📄 rootfs/INSTALL_README.txt     - USB 內說明"
echo "                                   On-USB instructions"
echo ""

sleep 2

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo -e "${GREEN}準備好建立您的 USB 安裝程式了嗎？${NC}"
echo -e "${GREEN}Ready to create your USB installer?${NC}"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

read -p "要現在建立嗎？(x86_64/arm64/n): Create now? (x86_64/arm64/n): " choice

case $choice in
    x86_64)
        echo ""
        echo "正在建立 x86_64 USB 安裝程式..."
        echo "Creating x86_64 USB installer..."
        ./tools/make-usb-installer.sh --arch x86_64
        ;;
    arm64)
        echo ""
        echo "正在建立 ARM64 USB 安裝程式..."
        echo "Creating ARM64 USB installer..."
        ./tools/make-usb-installer.sh --arch arm64
        ;;
    n|N)
        echo ""
        echo "已取消 / Cancelled"
        ;;
    *)
        echo ""
        echo "無效的選擇 / Invalid choice"
        echo "請執行: ./tools/make-usb-installer.sh --help"
        echo "Run: ./tools/make-usb-installer.sh --help"
        ;;
esac

echo ""
echo -e "${BLUE}感謝使用 ShirleyOS！${NC}"
echo -e "${BLUE}Thank you for using ShirleyOS!${NC}"
echo ""
