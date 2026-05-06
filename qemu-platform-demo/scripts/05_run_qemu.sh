#!/bin/bash
set -euo pipefail

KERNEL_DIR=${KERNEL_DIR:-linux-6.6.30}
KERNEL="${KERNEL_DIR}/arch/arm64/boot/Image"
DTB="dts/qemu-virt-myled.dtb"
INITRAMFS="rootfs/initramfs.cpio.gz"

echo "╔══════════════════════════════════════════════════════════╗"
echo "║  Launching QEMU ARM64 — Platform Driver Demo            ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "  Kernel   : ${KERNEL}"
echo "  DTB      : ${DTB}"
echo "  Initramfs: ${INITRAMFS}"
echo ""
echo "  Press Ctrl-A X to exit QEMU"
echo ""

qemu-system-aarch64 \
    -machine virt           \
    -cpu cortex-a57         \
    -m 512M                 \
    -nographic              \
    -kernel  "${KERNEL}"    \
    -dtb     "${DTB}"       \
    -initrd  "${INITRAMFS}" \
    -append  "console=ttyAMA0 earlycon=pl011,0x9000000 rdinit=/init loglevel=7"
