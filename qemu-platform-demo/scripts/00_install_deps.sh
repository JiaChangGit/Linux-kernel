#!/bin/bash
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
 make \
 git \
 wget
echo "✔  Dependencies installed"
