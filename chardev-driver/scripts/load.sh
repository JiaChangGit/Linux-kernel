#!/bin/bash
set -e

# 專案根目錄：用腳本位置推回去，避免從不同目錄執行時找錯路徑。
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DRIVER_DIR="$ROOT_DIR/driver"

# kernel module 必須使用目前 kernel 對應的 build directory。
KDIR="/lib/modules/$(uname -r)/build"

echo "[*] Project root: $ROOT_DIR"
echo "[*] Driver dir   : $DRIVER_DIR"
echo "[*] Kernel dir   : $KDIR"

# 基本檢查：先確認 driver 目錄與 Makefile 存在，再交給 kernel build system。
if [ ! -d "$DRIVER_DIR" ]; then
    echo "[X] Driver directory not found: $DRIVER_DIR"
    exit 1
fi

if [ ! -f "$DRIVER_DIR/Makefile" ]; then
    echo "[X] Missing Makefile in driver directory"
    exit 1
fi

if [ ! -d "$KDIR" ]; then
    echo "[X] Kernel build directory not found: $KDIR"
    echo "    Install matching kernel headers, or use an environment with kernel build files."
    exit 1
fi

echo "[*] Building driver..."
make -C "$KDIR" M="$DRIVER_DIR" modules

echo "[*] Loading module..."
if lsmod | grep -q "^chardev "; then
    echo "[!] chardev module is already loaded; skip insmod."
else
    sudo insmod "$DRIVER_DIR/chardev.ko"
fi

echo "[*] Verifying device..."
ls -la /dev/chardev0 || echo "[!] /dev/chardev0 not created yet"

# 教學環境方便測試用。正式環境應依需求設定 udev rule 或權限政策。
echo "[*] Setting permissions..."
sudo chmod 666 /dev/chardev0 2>/dev/null || true

echo "[+] Done."

echo "[*] Module info:"
modinfo "$DRIVER_DIR/chardev.ko" | grep -E "description|version|author|license" || true

echo ""
echo "[*] dmesg (last 10 lines):"
sudo dmesg | tail -10
