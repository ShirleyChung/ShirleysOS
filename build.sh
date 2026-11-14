#!/usr/bin/env bash
# ==============================================================================
# ShirleyOS Unified Build & Update Package Creator
# ==============================================================================
# Usage:
#   ./build.sh [target] [options]
# Targets:
#   x86_64 (default), x86_64_uefi, arm64, arm64_uefi, apple_silicon, all
# Options:
#   --quick          Only build without generating distribution tarball
#   --output-dir DIR Output directory for packages (default: dist/)
#   -h, --help       Show this help message
# ==============================================================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIST_DIR="$ROOT_DIR/dist"
TARGET="x86_64"
QUICK_BUILD=0

# 解析參數
while [[ $# -gt 0 ]]; do
    case "$1" in
        x86_64|amd64) TARGET="x86_64"; shift ;;
        x86_64_uefi)  TARGET="x86_64_uefi"; shift ;;
        arm64|aarch64) TARGET="arm64"; shift ;;
        arm64_uefi)   TARGET="arm64_uefi"; shift ;;
        apple_silicon) TARGET="apple_silicon"; shift ;;
        all)          TARGET="all"; shift ;;
        -t|--target)  TARGET="$2"; shift 2 ;;
        -q|--quick)   QUICK_BUILD=1; shift ;;
        -o|--output-dir) DIST_DIR="$2"; shift 2 ;;
        -h|--help)
            echo "ShirleyOS Unified Build & Packaging Tool"
            echo "Usage: $0 [target] [options]"
            echo "Targets: x86_64 (default), x86_64_uefi, arm64, arm64_uefi, apple_silicon, all"
            echo "Options:"
            echo "  --quick           Compile only, skip packaging tarballs"
            echo "  --output-dir DIR  Set package destination directory (default: dist/)"
            exit 0
            ;;
        *)
            echo "Error: Unknown argument '$1'" >&2
            exit 1
            ;;
    esac
done

VERSION="0.6.0"
COMMIT_HASH="$(git -C "$ROOT_DIR" rev-parse --short HEAD 2>/dev/null || echo "unknown")"
BUILD_DATE="$(date -u +"%Y-%m-%d %H:%M:%S UTC")"

build_single_target() {
    local tgt="$1"
    echo "=========================================================="
    echo "  [Build] Compiling ShirleyOS Target: $tgt"
    echo "=========================================================="
    
    # 呼叫底層建置腳本
    bash "$ROOT_DIR/scripts/build.sh" "$tgt"
    
    if [ "$QUICK_BUILD" -eq 1 ]; then
        return 0
    fi

    # 建立安裝升級包目錄
    echo "----------------------------------------------------------"
    echo "  [Package] Creating In-Place Update Package for $tgt"
    echo "----------------------------------------------------------"
    
    local pkg_name="shirleyos-${tgt}-update-${VERSION}"
    local pkg_dir="$DIST_DIR/${pkg_name}"
    rm -rf "$pkg_dir"
    mkdir -p "$pkg_dir/kernel" "$pkg_dir/boot" "$pkg_dir/drivers" "$pkg_dir/rootfs"
    
    # 尋找建置產物
    local build_make_dir="$ROOT_DIR/build/${tgt}-make"
    
    # 1. 複製核心 ELF 與純二進位
    if [ -f "$build_make_dir/shirley-kernel.elf" ]; then
        cp "$build_make_dir/shirley-kernel.elf" "$pkg_dir/kernel/"
    fi
    if [ -f "$build_make_dir/kernel.bin" ]; then
        cp "$build_make_dir/kernel.bin" "$pkg_dir/kernel/"
    fi
    if [ -f "$build_make_dir/boot.bin" ]; then
        cp "$build_make_dir/boot.bin" "$pkg_dir/kernel/"
    fi
    if [ -f "$build_make_dir/shirley-x86_64.img" ]; then
        cp "$build_make_dir/shirley-x86_64.img" "$pkg_dir/kernel/"
    fi

    # 2. 複製 EFI 開機檔案 (若有)
    if [ -d "$build_make_dir/esp" ]; then
        cp -r "$build_make_dir/esp/"* "$pkg_dir/boot/"
    elif [ -d "$ROOT_DIR/build/${tgt}_uefi-make/esp" ]; then
        cp -r "$ROOT_DIR/build/${tgt}_uefi-make/esp/"* "$pkg_dir/boot/"
    fi

    # 3. 複製高階 Rust 模組
    local rust_target="x86_64-unknown-none"
    if [[ "$tgt" == *"arm64"* ]] || [ "$tgt" = "apple_silicon" ]; then
        rust_target="aarch64-unknown-none"
    fi
    if [ -f "$ROOT_DIR/rust/target/${rust_target}/release/libshirley_rust.a" ]; then
        cp "$ROOT_DIR/rust/target/${rust_target}/release/libshirley_rust.a" "$pkg_dir/drivers/"
    fi

    # 4. 複製 rootfs
    if [ -d "$ROOT_DIR/rootfs" ]; then
        cp -r "$ROOT_DIR/rootfs/"* "$pkg_dir/rootfs/"
    fi

    # 5. 產生 VERSION 檔案
    cat > "$pkg_dir/VERSION" <<EOF
ShirleyOS Update Package
Version     : ${VERSION}
Target Arch : ${tgt}
Git Commit  : ${COMMIT_HASH}
Build Date  : ${BUILD_DATE}
Rust Module : Built & Linked (${rust_target})
Syscalls    : 17 ABI Compatible
EOF

    # 6. 產生直接在已安裝機器升級的升級腳本 upgrade.sh
    cat > "$pkg_dir/upgrade.sh" <<'UPGRADE_SCRIPT'
#!/usr/bin/env sh
# ==============================================================================
# ShirleyOS In-Place Upgrade Script
# Can be run directly on an installed system or from an external recovery shell
# ==============================================================================
set -eu

INSTALL_DIR="${1:-}"

if [ -z "$INSTALL_DIR" ]; then
    echo "Usage: ./upgrade.sh <mount_point_or_target_dir>"
    echo "Examples:"
    echo "  ./upgrade.sh /boot               (Direct on ShirleyOS / Linux)"
    echo "  ./upgrade.sh /Volumes/SHIRLEY_USB (External USB/Drive on macOS)"
    echo "  ./upgrade.sh /mnt/shirley        (Mounted partition)"
    exit 1
fi

if [ ! -d "$INSTALL_DIR" ]; then
    echo "[Error] Target directory '$INSTALL_DIR' does not exist." >&2
    exit 1
fi

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
echo "=========================================================="
echo "  Upgrading ShirleyOS on: $INSTALL_DIR"
echo "  Package: $(cat "$SCRIPT_DIR/VERSION" | grep "Version" || true)"
echo "=========================================================="

# 備份舊版核心
if [ -f "$INSTALL_DIR/shirley-kernel.elf" ]; then
    echo "[1/4] Backing up existing kernel to shirley-kernel.elf.bak..."
    cp "$INSTALL_DIR/shirley-kernel.elf" "$INSTALL_DIR/shirley-kernel.elf.bak"
fi
if [ -f "$INSTALL_DIR/EFI/BOOT/BOOTX64.EFI" ]; then
    cp "$INSTALL_DIR/EFI/BOOT/BOOTX64.EFI" "$INSTALL_DIR/EFI/BOOT/BOOTX64.EFI.bak"
fi
if [ -f "$INSTALL_DIR/EFI/BOOT/BOOTAA64.EFI" ]; then
    cp "$INSTALL_DIR/EFI/BOOT/BOOTAA64.EFI" "$INSTALL_DIR/EFI/BOOT/BOOTAA64.EFI.bak"
fi

# 更新核心二進位檔
echo "[2/4] Updating kernel binary & drivers..."
if [ -f "$SCRIPT_DIR/kernel/shirley-kernel.elf" ]; then
    cp "$SCRIPT_DIR/kernel/shirley-kernel.elf" "$INSTALL_DIR/"
fi
if [ -f "$SCRIPT_DIR/kernel/kernel.bin" ]; then
    cp "$SCRIPT_DIR/kernel/kernel.bin" "$INSTALL_DIR/"
fi

# 更新開機載入器
echo "[3/4] Updating EFI bootloaders..."
if [ -d "$SCRIPT_DIR/boot/EFI" ]; then
    mkdir -p "$INSTALL_DIR/EFI/BOOT"
    cp -r "$SCRIPT_DIR/boot/EFI/"* "$INSTALL_DIR/EFI/"
fi

# 更新系統資訊
echo "[4/4] Writing version manifest..."
cp "$SCRIPT_DIR/VERSION" "$INSTALL_DIR/etc/version" 2>/dev/null || cp "$SCRIPT_DIR/VERSION" "$INSTALL_DIR/"

sync || true
echo "=========================================================="
echo "  Upgrade successful! ShirleyOS has been updated."
echo "  Please reboot the machine to load the new kernel."
echo "=========================================================="
UPGRADE_SCRIPT
    chmod +x "$pkg_dir/upgrade.sh"

    # 7. 打包成 .tar.gz 與 .zip
    mkdir -p "$DIST_DIR"
    echo "  Creating tarball: dist/${pkg_name}.tar.gz"
    (cd "$DIST_DIR" && tar -czf "${pkg_name}.tar.gz" "${pkg_name}")
    if command -v zip >/dev/null 2>&1; then
        echo "  Creating zip archive: dist/${pkg_name}.zip"
        (cd "$DIST_DIR" && zip -rq "${pkg_name}.zip" "${pkg_name}")
    fi

    echo "  [Success] Update package generated at:"
    echo "    Folder  : $pkg_dir"
    echo "    Tarball : $DIST_DIR/${pkg_name}.tar.gz"
}

mkdir -p "$DIST_DIR"

if [ "$TARGET" = "all" ]; then
    build_single_target "x86_64"
    build_single_target "x86_64_uefi"
    build_single_target "arm64"
    build_single_target "arm64_uefi"
    build_single_target "apple_silicon"
else
    build_single_target "$TARGET"
fi

echo "=========================================================="
echo "  All build and packaging steps completed successfully!"
echo "=========================================================="
