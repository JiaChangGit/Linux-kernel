#!/bin/bash
set -e

# 從腳本位置推回專案根目錄，避免呼叫者目前目錄影響 clean 路徑。
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
KDIR="/lib/modules/$(uname -r)/build"

echo "[*] Removing module..."
if lsmod | grep -q "^chardev "; then
    sudo rmmod chardev
else
    echo "[!] chardev module is not loaded; skip rmmod."
fi

echo "[*] Verifying cleanup..."
ls /dev/chardev0 2>/dev/null && echo "WARNING: device still exists" || echo "[+] /dev/chardev0 removed"
ls /proc/chardev_info 2>/dev/null && echo "WARNING: proc entry still exists" || echo "[+] /proc/chardev_info removed"

echo "[+] dmesg:"
sudo dmesg | tail -3

echo "[*] Cleaning build outputs..."
if [ -d "$KDIR" ]; then
    make -C "$ROOT_DIR/driver" clean
else
    echo "[!] Kernel build directory not found; skip driver clean: $KDIR"
fi
make -C "$ROOT_DIR/userspace" clean
