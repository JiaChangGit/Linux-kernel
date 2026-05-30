#!/bin/bash
# 使用專案建好的 ARM64 kernel tree 編譯 myled_ctrl.ko。
set -euo pipefail

KERNEL_DIR=${1:-linux-6.6.30}
KDIR=$(realpath "${KERNEL_DIR}")

echo "=== Building out-of-tree module ==="
cd driver/
# 明確指定 KDIR，避免誤用主機目前正在跑的 kernel headers。
make KDIR="${KDIR}" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- all
make install
echo "myled_ctrl.ko built and copied to rootfs/overlay/"
