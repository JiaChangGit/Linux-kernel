#!/bin/bash
set -euo pipefail

KERNEL_VERSION="6.6.30"
KERNEL_DIR="linux-${KERNEL_VERSION}"
KERNEL_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/${KERNEL_DIR}.tar.xz"
JOBS=$(nproc)

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

echo "=== [1/3] Download kernel ${KERNEL_VERSION} ==="
if [ ! -d "${KERNEL_DIR}" ]; then
    wget -q --show-progress "${KERNEL_URL}"
    tar xf "${KERNEL_DIR}.tar.xz"
fi

echo "=== [2/3] Configure (defconfig) ==="
cd "${KERNEL_DIR}"
make defconfig

echo "=== [3/3] Build (${JOBS} jobs) ==="
make -j${JOBS} Image modules

echo ""
echo "✔  Kernel image: ${KERNEL_DIR}/arch/arm64/boot/Image"
