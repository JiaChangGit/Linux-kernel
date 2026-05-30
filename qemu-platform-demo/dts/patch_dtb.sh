#!/bin/bash
# 產生 QEMU final DTB：
#   1. 從 QEMU virt machine dump base DTB。
#   2. 檢查 myled MMIO 範圍沒有撞到既有 device reg。
#   3. 將 myled overlay 合併成 qemu-virt-myled.dtb。
set -euo pipefail

QEMU_BIN=${QEMU_BIN:-qemu-system-aarch64}
KERNEL=${1:-../Image}
OVERLAY_DTS=myled-fragment.dts
OVERLAY_DTBO=myled-fragment.dtbo
BASE_DTB=qemu-virt-base.dtb
FINAL_DTB=qemu-virt-myled.dtb
MYLED_BASE=$((0x0d000000))
MYLED_SIZE=$((0x1000))

check_myled_reg_conflict() {
    local target_end=$((MYLED_BASE + MYLED_SIZE - 1))
    local node="<unknown>"
    local line
    local cells
    local i
    local base
    local size
    local end

    while IFS= read -r line; do
        if [[ "${line}" =~ ^[[:space:]]*([A-Za-z0-9,._+-]+@[0-9A-Fa-f]+)[[:space:]]*\{ ]]; then
            node="${BASH_REMATCH[1]}"
        fi

        [[ "${line}" == *"reg = <"* ]] || continue
        line="${line#*<}"
        line="${line%%>*}"
        read -r -a cells <<< "${line}"

        # QEMU virt 使用 64-bit address/size；四個 cell 才是一組 reg。
        # 只檢查 device reg，不把 bus ranges 誤當成已佔用裝置。
        if (( ${#cells[@]} == 0 || ${#cells[@]} % 4 != 0 )); then
            continue
        fi

        for ((i = 0; i < ${#cells[@]}; i += 4)); do
            base=$(((${cells[i]} << 32) + ${cells[i + 1]}))
            size=$(((${cells[i + 2]} << 32) + ${cells[i + 3]}))
            (( size > 0 )) || continue
            end=$((base + size - 1))

            if (( MYLED_BASE <= end && target_end >= base )); then
                printf 'ERROR: myled MMIO 0x%08x-0x%08x overlaps %s reg 0x%x-0x%x\n' \
                    "${MYLED_BASE}" "${target_end}" "${node}" "${base}" "${end}" >&2
                return 1
            fi
        done
    done < <(dtc -I dtb -O dts "${BASE_DTB}" 2>/dev/null)
}

echo "[1/5] Dump QEMU virt DTB ..."
QEMU_ARGS=(-machine "virt,dumpdtb=${BASE_DTB}" -cpu cortex-a57 -nographic)
if [ -f "${KERNEL}" ]; then
    QEMU_ARGS+=(-kernel "${KERNEL}")
else
    echo "Kernel image not found for dumpdtb; dumping base DTB without -kernel"
fi

"${QEMU_BIN}" "${QEMU_ARGS[@]}" 2>/dev/null || true
if [ ! -s "${BASE_DTB}" ]; then
    echo "ERROR: failed to dump ${BASE_DTB}"
    exit 1
fi

echo "[2/5] Check myled MMIO range ..."
check_myled_reg_conflict

echo "[3/5] Compile overlay DTS to DTBO ..."
dtc -Wno-unit_address_format -I dts -O dtb -@ -o "${OVERLAY_DTBO}" "${OVERLAY_DTS}"

echo "[4/5] Merge base DTB + overlay ..."
fdtoverlay -i "${BASE_DTB}" -o "${FINAL_DTB}" "${OVERLAY_DTBO}"

echo "[5/5] Verify merged node ..."
dtc -I dtb -O dts -o /dev/stdout "${FINAL_DTB}" 2>/dev/null \
    | grep -A 10 "myled-controller@0d000000"

echo "Final DTB: ${FINAL_DTB}"
