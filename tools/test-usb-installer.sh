#!/bin/bash
# Test USB Installer Creation
# 測試 USB 安裝程式建立

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[Test]${NC} $1"
}

error() {
    echo -e "${RED}[Error]${NC} $1"
    exit 1
}

log "測試 USB 安裝程式建立工具"
log "Testing USB installer creation tool"

# 檢查腳本是否存在
if [[ ! -f "${SCRIPT_DIR}/make-usb-installer.sh" ]]; then
    error "找不到 make-usb-installer.sh"
fi

log "✓ make-usb-installer.sh 存在"

# 檢查是否可執行
if [[ ! -x "${SCRIPT_DIR}/make-usb-installer.sh" ]]; then
    error "make-usb-installer.sh 不可執行"
fi

log "✓ make-usb-installer.sh 可執行"

# 測試 help 選項
log "測試 --help 選項..."
"${SCRIPT_DIR}/make-usb-installer.sh" --help > /dev/null 2>&1
log "✓ --help 選項正常"

# 檢查文件是否存在
if [[ ! -f "${PROJECT_ROOT}/docs/USB_INSTALLER.md" ]]; then
    error "找不到 USB_INSTALLER.md"
fi

log "✓ USB_INSTALLER.md 存在"

if [[ ! -f "${PROJECT_ROOT}/rootfs/INSTALL_README.txt" ]]; then
    error "找不到 INSTALL_README.txt"
fi

log "✓ INSTALL_README.txt 存在"

# 檢查建置工具
log "檢查建置工具..."
if command -v hdiutil &> /dev/null; then
    log "✓ hdiutil 可用 (macOS)"
elif command -v parted &> /dev/null; then
    log "✓ parted 可用 (Linux)"
else
    error "找不到必要的磁碟工具"
fi

log ""
log "======================================"
log "所有測試通過！"
log "All tests passed!"
log "======================================"
log ""
log "您可以執行以下指令建立 USB 安裝程式："
log "You can create a USB installer with:"
log ""
log "  ./tools/make-usb-installer.sh --arch x86_64"
log "  ./tools/make-usb-installer.sh --arch arm64"
log ""
