#!/bin/bash
set -euo pipefail

ROOTFS_DIR="rootfs/initramfs"
OVERLAY_DIR="rootfs/overlay"
OUTPUT="rootfs/initramfs.cpio.gz"

echo "=== Building minimal initramfs ==="

rm -rf "${ROOTFS_DIR}"
mkdir -p "${ROOTFS_DIR}"/{bin,sbin,etc,proc,sys,dev,lib,lib64,usr/bin}

# ── BusyBox ────────────────────────────────────────────────────────
#BUSYBOX=$(which busybox-aarch64 2>/dev/null \
#          || ls /usr/bin/busybox* 2>/dev/null | head -1 \
#          || echo busybox)
## !!! Change to your path
BUSYBOX=~/桌面/Linux-kernel/qemu-platform-demo/busybox-1.36.1/busybox

cp "${BUSYBOX}" "${ROOTFS_DIR}/bin/busybox"

cd "${ROOTFS_DIR}"
for app in sh ls cat echo mount insmod dmesg grep sed; do
    ln -sf /bin/busybox "bin/${app}" 2>/dev/null || true
done
cd -

# ── Overlay files (init, test script, .ko) ────────────────────────
cp "${OVERLAY_DIR}/init"          "${ROOTFS_DIR}/init"
cp "${OVERLAY_DIR}/test_myled.sh" "${ROOTFS_DIR}/test_myled.sh"
cp "${OVERLAY_DIR}/myled_ctrl.ko" "${ROOTFS_DIR}/myled_ctrl.ko"

chmod +x "${ROOTFS_DIR}/init" "${ROOTFS_DIR}/test_myled.sh"

# ── Pack ──────────────────────────────────────────────────────────
cd "${ROOTFS_DIR}"
find . | cpio -H newc -o | gzip -9 > "../../${OUTPUT}"
cd -

echo "✔  Initramfs: ${OUTPUT}  ($(du -sh ${OUTPUT} | cut -f1))"
