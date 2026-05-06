#!/bin/bash
set -euo pipefail

KERNEL_DIR=${1:-linux-6.6.30}
KDIR=$(realpath "${KERNEL_DIR}")

echo "=== Building out-of-tree module ==="
cd driver/
make KDIR="${KDIR}" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- all
make install   # copies .ko → rootfs/overlay/
echo "✔  myled_ctrl.ko built and copied to rootfs/overlay/"
