#!/bin/bash
# cleanup.sh — 清理 qemu-platform-demo 專案的所有建置產物
# 使用方式：bash cleanup.sh [選項]
#   --all        完整清理，包含下載的 kernel source
#   --soft       只清理建置產物，保留 kernel source（預設）
#   --dry-run    只列出會刪除的東西，不真的刪

set -euo pipefail

# ── 顏色輸出 ────────────────────────────────────────────────────────
RED='\033[0;31m'; YELLOW='\033[1;33m'; GREEN='\033[0;32m'
CYAN='\033[0;36m'; NC='\033[0m'

info()    { echo -e "${CYAN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
success() { echo -e "${GREEN}[OK]${NC}    $*"; }
removed() { echo -e "${RED}[DEL]${NC}   $*"; }

# ── 參數解析 ────────────────────────────────────────────────────────
MODE="soft"
DRY_RUN=false

for arg in "$@"; do
    case $arg in
        --all)     MODE="all"  ;;
        --soft)    MODE="soft" ;;
        --dry-run) DRY_RUN=true ;;
        --help|-h)
            echo "Usage: bash cleanup.sh [--all|--soft] [--dry-run]"
            echo ""
            echo "  --soft     (預設) 清理建置產物，保留 kernel source tarball 與目錄"
            echo "  --all      完整清理，連 kernel source / tarball 一起刪"
            echo "  --dry-run  只列出將被刪除的項目，不執行任何刪除"
            exit 0
            ;;
        *)
            warn "未知選項: $arg，忽略"
            ;;
    esac
done

# ── 安全確認（非 dry-run 時詢問） ───────────────────────────────────
if [ "$DRY_RUN" = false ]; then
    echo ""
    echo -e "${YELLOW}即將清理環境（模式: ${MODE}）${NC}"
    [ "$MODE" = "all" ] && \
        warn "--all 模式將刪除 kernel source，需重新下載編譯（耗時）！"
    echo ""
    read -rp "確定要繼續？[y/N] " confirm
    [[ "$confirm" =~ ^[Yy]$ ]] || { info "已取消"; exit 0; }
    echo ""
fi

# ── 輔助函式 ────────────────────────────────────────────────────────
do_remove() {
    local target="$1"
    if [ -e "$target" ] || [ -L "$target" ]; then
        if [ "$DRY_RUN" = true ]; then
            removed "(dry-run) $target"
        else
            rm -rf "$target"
            removed "$target"
        fi
    fi
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SCRIPT_DIR"

# ════════════════════════════════════════════════════════════════════
info "=== 1. 清理 driver 建置產物 ==="
# ════════════════════════════════════════════════════════════════════
DRIVER_ARTIFACTS=(
    driver/*.ko
    driver/*.o
    driver/*.mod
    driver/*.mod.c
    driver/*.mod.o
    driver/.*.cmd
    driver/.tmp_versions
    driver/Module.symvers
    driver/modules.order
    driver/build/
)
for item in "${DRIVER_ARTIFACTS[@]}"; do
    # glob 可能不展開，用 for 逐一處理
    for f in $item; do
        do_remove "$f"
    done
done
success "driver 建置產物清理完成"

# ════════════════════════════════════════════════════════════════════
info "=== 2. 清理 DTB 相關產物 ==="
# ════════════════════════════════════════════════════════════════════
DTB_ARTIFACTS=(
    dts/qemu-virt-base.dtb
    dts/qemu-virt-myled.dtb
    dts/myled-fragment.dtbo
)
for item in "${DTB_ARTIFACTS[@]}"; do
    do_remove "$item"
done
success "DTB 產物清理完成"

# ════════════════════════════════════════════════════════════════════
info "=== 3. 清理 rootfs 建置產物 ==="
# ════════════════════════════════════════════════════════════════════
ROOTFS_ARTIFACTS=(
    rootfs/initramfs/
    rootfs/initramfs.cpio.gz
    rootfs/overlay/myled_ctrl.ko   # driver install 複製過來的
)
for item in "${ROOTFS_ARTIFACTS[@]}"; do
    do_remove "$item"
done
success "rootfs 建置產物清理完成"

# ════════════════════════════════════════════════════════════════════
info "=== 4. 清理 kernel 建置產物（保留 source） ==="
# ════════════════════════════════════════════════════════════════════

# 找出所有解壓縮過的 kernel 目錄
shopt -s nullglob
KERNEL_DIRS=(linux-* busybox-*)
shopt -u nullglob

if [ "${#KERNEL_DIRS[@]}" -gt 0 ]; then
    for kdir in "${KERNEL_DIRS[@]}"; do
        if [ -f "${kdir}/Makefile" ]; then
            info "清理 kernel 建置產物：${kdir}"
            if [ "$DRY_RUN" = true ]; then
                removed "(dry-run) make mrproper in ${kdir}"
            else
                # mrproper 比 clean 更徹底，會清除 .config
                make -C "${kdir}" \
                    ARCH=arm64 \
                    CROSS_COMPILE=aarch64-linux-gnu- \
                    mrproper -j"$(nproc)" \
                    2>/dev/null && removed "${kdir}/ (mrproper)" \
                              || warn "${kdir}: mrproper 失敗，嘗試直接刪除 build 產物"
            fi
        fi
    done
else
    info "未找到 kernel source 目錄，跳過"
fi
success "kernel 建置產物清理完成"

# ════════════════════════════════════════════════════════════════════
# --all 模式：連 kernel source 和 tarball 一起刪
# ════════════════════════════════════════════════════════════════════
if [ "$MODE" = "all" ]; then
    info "=== 5. [--all] 刪除 kernel source 目錄與 tarball ==="

    for kdir in "${KERNEL_DIRS[@]-}"; do
        do_remove "$kdir"
    done

    for tarball in linux-*.tar.xz linux-*.tar.gz busybox-*.tar.bz2; do
        [ -e "$tarball" ] && do_remove "$tarball"
    done

    success "kernel source 完整清理完成"
fi

# ════════════════════════════════════════════════════════════════════
info "=== 清理暫存與 log 檔案 ==="
# ════════════════════════════════════════════════════════════════════
MISC_ARTIFACTS=(
    *.log
    *.tmp
    .build_cache/
)
for item in "${MISC_ARTIFACTS[@]}"; do
    for f in $item; do
        [ -e "$f" ] && do_remove "$f"
    done
done

# ════════════════════════════════════════════════════════════════════
echo ""
if [ "$DRY_RUN" = true ]; then
    echo -e "${YELLOW}=== Dry-run 完成，以上項目實際上未被刪除 ===${NC}"
else
    echo -e "${GREEN}=== 環境清理完成 ===${NC}"
    echo ""
    echo "重新建置流程："
    echo "  bash scripts/01_build_kernel.sh   # ← --all 模式後需重做"
    echo "  bash scripts/02_patch_dtb.sh"
    echo "  bash scripts/03_build_driver.sh"
    echo "  bash scripts/04_build_rootfs.sh"
    echo "  bash scripts/05_run_qemu.sh"
    echo "  bash scripts/07_fix_busybox_arch.sh"
    echo "  07 => for kernel 是 ARM64，busybox 是 x86-64，kernel 無法執行 /bin/sh，所以 init 直接失敗。"
    echo "  07-> 03-> 04-> 05"
fi
echo ""
