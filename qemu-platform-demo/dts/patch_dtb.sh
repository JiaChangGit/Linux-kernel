#!/bin/bash
# patch_dtb.sh — Dump QEMU DTB, merge our overlay, produce final DTB
set -euo pipefail

QEMU_BIN=${QEMU_BIN:-qemu-system-aarch64}
KERNEL=${1:-../Image}
OVERLAY_DTS=myled-fragment.dts
OVERLAY_DTBO=myled-fragment.dtbo
BASE_DTB=qemu-virt-base.dtb
FINAL_DTB=qemu-virt-myled.dtb

echo "[1/4] Dump QEMU virt DTB ..."
${QEMU_BIN} \
    -machine virt,dumpdtb=${BASE_DTB} \
    -cpu cortex-a57 \
    -kernel ${KERNEL} \
    -nographic 2>/dev/null || true

echo "[2/4] Compile overlay DTS → DTBO ..."
dtc -I dts -O dtb -@ -o ${OVERLAY_DTBO} ${OVERLAY_DTS}

echo "[3/4] Merge base DTB + overlay ..."
fdtoverlay -i ${BASE_DTB} -o ${FINAL_DTB} ${OVERLAY_DTBO}

echo "[4/4] Verify merged node ..."
dtc -I dtb -O dts -o /dev/stdout ${FINAL_DTB} 2>/dev/null \
    | grep -A 10 "myled-controller"

echo "✔  Final DTB: ${FINAL_DTB}"
