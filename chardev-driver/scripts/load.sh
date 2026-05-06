#!/bin/bash
set -e

# 取得專案根目錄（避免相對路徑問題）
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DRIVER_DIR="$ROOT_DIR/driver"

KDIR="/lib/modules/$(uname -r)/build"

echo "[*] Project root: $ROOT_DIR"
echo "[*] Driver dir   : $DRIVER_DIR"
echo "[*] Kernel dir   : $KDIR"

# === sanity check ===
if [ ! -d "$DRIVER_DIR" ]; then
    echo "[X] Driver directory not found: $DRIVER_DIR"
    exit 1
fi

if [ ! -f "$DRIVER_DIR/Makefile" ]; then
    echo "[X] Missing Makefile in driver directory"
    exit 1
fi

echo "[*] Building driver..."
make -C "$KDIR" M="$DRIVER_DIR" modules

echo "[*] Loading module..."
sudo insmod "$DRIVER_DIR/chardev.ko" || true

echo "[*] Verifying device..."
ls -la /dev/chardev0 || echo "[!] /dev/chardev0 not created yet"

echo "[*] Setting permissions..."
sudo chmod 666 /dev/chardev0 2>/dev/null || true

echo "[+] Done."

echo "[*] Module info:"
modinfo "$DRIVER_DIR/chardev.ko" | grep -E "description|version|author|license" || true

echo ""
echo "[*] dmesg (last 10 lines):"
sudo dmesg | tail -10
