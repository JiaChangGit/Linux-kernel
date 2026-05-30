#!/bin/bash
# 建立最小 initramfs。
# 內容包含 ARM64 BusyBox、/init、自測腳本與 myled_ctrl.ko。
set -euo pipefail

ROOTFS_DIR="rootfs/initramfs"
OVERLAY_DIR="rootfs/overlay"
OUTPUT="rootfs/initramfs.cpio.gz"
TOOLS_BUSYBOX="tools/busybox-aarch64"

echo "=== Building minimal initramfs ==="

# ARM64 kernel 只能執行 ARM64 BusyBox；架構不符會讓 /init 啟動失敗。
if [ -n "${BUSYBOX:-}" ]; then
    BUSYBOX_BIN="${BUSYBOX}"
elif [ -x "${TOOLS_BUSYBOX}" ]; then
    BUSYBOX_BIN="${TOOLS_BUSYBOX}"
else
    echo "ERROR: ARM64 BusyBox not found."
    echo "Run: bash scripts/0A_fix_busybox_arch.sh"
    echo "Or set BUSYBOX=/path/to/aarch64/busybox"
    exit 1
fi

if [ ! -x "${BUSYBOX_BIN}" ]; then
    echo "ERROR: BusyBox is not executable: ${BUSYBOX_BIN}"
    exit 1
fi

if command -v file >/dev/null 2>&1; then
    BUSYBOX_INFO=$(file -b "${BUSYBOX_BIN}")
    case "${BUSYBOX_INFO}" in
        *"ARM aarch64"*|*"ARM64"*|*"aarch64"*)
            ;;
        *)
            echo "ERROR: BusyBox is not an ARM64 binary: ${BUSYBOX_INFO}"
            exit 1
            ;;
    esac
fi

# 缺少 driver module 時直接停止，避免打包出無法測試的 initramfs。
if [ ! -f "${OVERLAY_DIR}/myled_ctrl.ko" ]; then
    echo "ERROR: ${OVERLAY_DIR}/myled_ctrl.ko not found."
    echo "Run: bash scripts/03_build_driver.sh"
    exit 1
fi

rm -rf "${ROOTFS_DIR}"
mkdir -p "${ROOTFS_DIR}"/{bin,sbin,etc,proc,sys,dev,lib,lib64,usr/bin}

cp "${BUSYBOX_BIN}" "${ROOTFS_DIR}/bin/busybox"

pushd "${ROOTFS_DIR}" >/dev/null
# 只建立測試會用到的 BusyBox applet，讓 initramfs 內容保持可預期。
for app in sh ls cat echo mount insmod dmesg grep sed readlink mdev cttyhack setsid sleep; do
    ln -sf /bin/busybox "bin/${app}"
done
popd >/dev/null

cp "${OVERLAY_DIR}/init" "${ROOTFS_DIR}/init"
cp "${OVERLAY_DIR}/test_myled.sh" "${ROOTFS_DIR}/test_myled.sh"
cp "${OVERLAY_DIR}/myled_ctrl.ko" "${ROOTFS_DIR}/myled_ctrl.ko"

chmod +x "${ROOTFS_DIR}/init" "${ROOTFS_DIR}/test_myled.sh"

pushd "${ROOTFS_DIR}" >/dev/null
find . | cpio --quiet -H newc -o | gzip -9 > "../../${OUTPUT}"
popd >/dev/null

echo "Initramfs: ${OUTPUT} ($(du -sh "${OUTPUT}" | cut -f1))"
