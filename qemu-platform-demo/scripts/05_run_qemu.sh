#!/bin/bash
# 啟動 QEMU ARM64 virt machine，載入 kernel、DTB 與 initramfs。
set -euo pipefail

KERNEL_DIR=${KERNEL_DIR:-linux-6.6.30}
KERNEL="${KERNEL_DIR}/arch/arm64/boot/Image"
DTB="dts/qemu-virt-myled.dtb"
INITRAMFS="rootfs/initramfs.cpio.gz"

# 先檢查三個必要產物，讓錯誤停在啟動前而不是 QEMU 開機中。
for f in "${KERNEL}" "${DTB}" "${INITRAMFS}"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: missing required file: $f"
        exit 1
    fi
done

echo "=========================================================="
echo " Launching QEMU ARM64 - Platform Driver Demo"
echo "=========================================================="
echo ""
echo "  Kernel   : ${KERNEL}"
echo "  DTB      : ${DTB}"
echo "  Initramfs: ${INITRAMFS}"
echo ""
echo "  Press Ctrl-A X to exit QEMU"
echo ""

# console 保持精簡；myled 細節由 /init 從 dmesg 篩出來。
qemu-system-aarch64 \
    -machine virt           \
    -cpu cortex-a57         \
    -m 512M                 \
    -nographic              \
    -kernel  "${KERNEL}"    \
    -dtb     "${DTB}"       \
    -initrd  "${INITRAMFS}" \
    -append  "console=ttyAMA0 earlycon=pl011,0x9000000 rdinit=/init loglevel=4"
