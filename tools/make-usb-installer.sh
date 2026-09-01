#!/bin/bash
# ShirleyOS USB Installer Creator
# 為 ShirleyOS 建立可開機 USB 安裝程式
# Creates a bootable USB installer for ShirleyOS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="${PROJECT_ROOT}/usb-installer"
IMAGE_SIZE_MB=512

# 顏色輸出 / Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[USB-Installer]${NC} $1"
}

error() {
    echo -e "${RED}[Error]${NC} $1" >&2
    exit 1
}

warn() {
    echo -e "${YELLOW}[Warning]${NC} $1"
}

usage() {
    cat <<EOF
用法 / Usage: $0 [OPTIONS]

選項 / Options:
    -a, --arch ARCH      目標架構: x86_64 或 arm64 (預設: x86_64)
                         Target architecture: x86_64 or arm64 (default: x86_64)
    -o, --output FILE    輸出映像檔路徑 (預設: shirleyos-installer.img)
                         Output image file path (default: shirleyos-installer.img)
    -s, --size SIZE      映像大小（MB）(預設: 512)
                         Image size in MB (default: 512)
    -h, --help           顯示此說明 / Show this help

範例 / Examples:
    # 創建 x86_64 映像檔
    $0 --arch x86_64

    # 創建 ARM64 映像檔
    $0 --arch arm64
EOF
    exit 0
}

# 解析參數 / Parse arguments
ARCH="x86_64"
OUTPUT_IMAGE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -a|--arch)
            ARCH="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_IMAGE="$2"
            shift 2
            ;;
        -s|--size)
            IMAGE_SIZE_MB="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            error "未知選項: $1"
            ;;
    esac
done

if [[ -z "$OUTPUT_IMAGE" ]]; then
    OUTPUT_IMAGE="${OUTPUT_DIR}/shirleyos-${ARCH}-installer.img"
fi

if [[ "$ARCH" != "x86_64" ]] && [[ "$ARCH" != "arm64" ]]; then
    error "不支援的架構: $ARCH"
fi

log "開始建立 ShirleyOS USB 安裝程式"
log "目標架構: $ARCH"

mkdir -p "$OUTPUT_DIR"

# 建置 ShirleyOS
log "建置 ShirleyOS..."
BUILD_TARGET="${ARCH}_uefi"
BUILD_DIR="${PROJECT_ROOT}/build-${BUILD_TARGET}"

if [[ ! -d "$BUILD_DIR" ]]; then
    log "執行建置..."
    "${PROJECT_ROOT}/shirley" build "$BUILD_TARGET" || error "建置失敗"
fi

# 檢查建置產物
KERNEL_ELF="${BUILD_DIR}/kernel/shirley.elf"

if [[ "$ARCH" == "x86_64" ]]; then
    BOOTLOADER_EFI="${BUILD_DIR}/boot/uefi/BOOTX64.EFI"
elif [[ "$ARCH" == "arm64" ]]; then
    BOOTLOADER_EFI="${BUILD_DIR}/boot/uefi/BOOTAA64.EFI"
fi

if [[ ! -f "$KERNEL_ELF" ]]; then
    error "找不到核心映像: $KERNEL_ELF"
fi

if [[ ! -f "$BOOTLOADER_EFI" ]]; then
    error "找不到開機載入器: $BOOTLOADER_EFI"
fi

log "找到核心: $KERNEL_ELF"
log "找到開機載入器: $BOOTLOADER_EFI"

# 建立映像 (macOS)
log "建立 ${IMAGE_SIZE_MB}MB 磁碟映像..."

hdiutil create -size "${IMAGE_SIZE_MB}m" -fs "MS-DOS FAT32" \
    -volname "SHIRLEY_USB" -layout GPTSPUD -ov "$OUTPUT_IMAGE" || \
    error "建立映像失敗"

log "掛載映像..."
hdiutil attach "$OUTPUT_IMAGE" || error "掛載失敗"

sleep 2

MOUNT_POINT="/Volumes/SHIRLEY_USB"

if [[ ! -d "$MOUNT_POINT" ]]; then
    error "掛載點不存在"
fi

log "建立 EFI 目錄結構..."
mkdir -p "${MOUNT_POINT}/EFI/BOOT"
mkdir -p "${MOUNT_POINT}/shirley"

log "複製檔案..."
cp "$BOOTLOADER_EFI" "${MOUNT_POINT}/EFI/BOOT/"
cp "$KERNEL_ELF" "${MOUNT_POINT}/shirley/kernel.elf"

if [[ -d "${PROJECT_ROOT}/rootfs" ]]; then
    cp -r "${PROJECT_ROOT}/rootfs" "${MOUNT_POINT}/shirley/"
fi

cat > "${MOUNT_POINT}/README.txt" <<'README'
ShirleyOS USB Installer

This USB contains a bootable ShirleyOS installation.

To boot:
1. Insert USB into computer
2. Select USB boot in BIOS/UEFI
3. ShirleyOS will start automatically
README

sync
log "卸載映像..."
hdiutil detach "$MOUNT_POINT"

log "======================================"
log "USB 安裝程式建立完成！"
log "======================================"
log "映像位置: $OUTPUT_IMAGE"
log "檔案大小: $(du -h "$OUTPUT_IMAGE" | cut -f1)"
log ""
log "寫入 USB 步驟:"
log "1. diskutil list"
log "2. diskutil unmountDisk /dev/diskN"
log "3. sudo dd if=$OUTPUT_IMAGE of=/dev/rdiskN bs=1m"
log "4. diskutil eject /dev/diskN"
