#!/bin/bash
# 下載並建置 ARM64 Linux kernel image。
# 產物會供 QEMU boot、DTB dump 與 out-of-tree module build 使用。
set -euo pipefail

KERNEL_VERSION="6.6.30"
KERNEL_DIR="linux-${KERNEL_VERSION}"
KERNEL_URL="https://cdn.kernel.org/pub/linux/kernel/v6.x/${KERNEL_DIR}.tar.xz"
JOBS=$(nproc)
MAX_CLOCK_SKEW_RETRIES=2

export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

normalize_future_timestamps() {
    local path="$1"

    [ -d "${path}" ] || return 0

    # WSL/Windows 時間同步偶爾會讓檔案看起來來自未來。
    # 只修正未來時間，避免每次都觸發 kernel tree 大量重編。
    find "${path}" -newermt now -exec touch -h -c {} +
}

run_make_checked() {
    local log_file
    local attempt
    local rc

    for attempt in $(seq 0 "${MAX_CLOCK_SKEW_RETRIES}"); do
        log_file=$(mktemp)

        set +e
        make -j"${JOBS}" Image modules >"${log_file}" 2>&1
        rc=$?
        set -e

        if [ "${rc}" -ne 0 ]; then
            cat "${log_file}"
            rm -f "${log_file}"
            return "${rc}"
        fi

        if grep -Eq 'Clock skew detected|has modification time .* in the future' "${log_file}"; then
            rm -f "${log_file}"
            if [ "${attempt}" -lt "${MAX_CLOCK_SKEW_RETRIES}" ]; then
                echo "Detected future build timestamps; normalizing and retrying..."
                normalize_future_timestamps "."
                continue
            fi

            echo "ERROR: clock skew persisted after timestamp normalization"
            return 1
        fi

        rm -f "${log_file}"
        return 0
    done
}

echo "=== [1/3] Download kernel ${KERNEL_VERSION} ==="
if [ ! -d "${KERNEL_DIR}" ]; then
    wget -q --show-progress "${KERNEL_URL}"
    tar xf "${KERNEL_DIR}.tar.xz"
fi

echo "=== [2/3] Configure (defconfig) ==="
cd "${KERNEL_DIR}"
normalize_future_timestamps "."
make defconfig

echo "=== [3/3] Build (${JOBS} jobs) ==="
run_make_checked

echo ""
echo "Kernel image: ${KERNEL_DIR}/arch/arm64/boot/Image"
