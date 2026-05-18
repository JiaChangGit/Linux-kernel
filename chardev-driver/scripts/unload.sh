#!/bin/bash

echo "[*] Removing module..."
sudo rmmod chardev

echo "[*] Verifying cleanup..."
ls /dev/chardev0 2>/dev/null && echo "WARNING: device still exists" || echo "[+] /dev/chardev0 removed"
ls /proc/chardev_info 2>/dev/null && echo "WARNING: proc entry still exists" || echo "[+] /proc/chardev_info removed"

echo "[+] dmesg:"
sudo dmesg | tail -3

cd driver
make clean
cd ..
