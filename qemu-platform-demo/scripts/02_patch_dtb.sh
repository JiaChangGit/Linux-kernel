#!/bin/bash
# 封裝 dts/patch_dtb.sh，使用指定 kernel Image 產生 final DTB。
set -euo pipefail

KERNEL_DIR=${1:-linux-6.6.30}
KERNEL_IMAGE="${KERNEL_DIR}/arch/arm64/boot/Image"

echo "=== Patching QEMU DTB ==="
cd dts/
bash patch_dtb.sh "../${KERNEL_IMAGE}"
echo "DTB ready: dts/qemu-virt-myled.dtb"
