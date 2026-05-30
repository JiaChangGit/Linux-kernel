#!/bin/bash
# 安裝本專案需要的建置與 QEMU 工具。
# 只處理套件，不修改專案產物。
set -euo pipefail
echo "Installing build dependencies ..."
sudo apt-get update

sudo apt-get install -y \
 gcc-aarch64-linux-gnu \
 g++-aarch64-linux-gnu \
 qemu-system-arm \
 device-tree-compiler \
 libfdt-dev \
 busybox-static \
 cpio \
 bc \
 bison \
 flex \
 libssl-dev \
 libelf-dev \
 file \
 make \
 git \
 wget
echo "Dependencies installed"
