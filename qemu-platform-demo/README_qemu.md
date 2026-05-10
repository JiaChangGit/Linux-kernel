# QEMU Platform Driver Demo (ARM64)

## Overview

This project demonstrates how to:

- Build an ARM64 Linux kernel
- Patch QEMU Device Tree Blob (DTB)
- Develop an out-of-tree platform driver
- Build a minimal initramfs
- Boot a custom ARM64 Linux system in QEMU
- Verify platform driver functionality

The demo environment uses:

- QEMU ARM64 (`virt` machine)
- Linux Kernel 6.6.x
- BusyBox rootfs
- Device Tree overlay / patching
- Platform driver + sysfs interaction

---

# Environment Setup

## 0. Clone Repository and Install Dependencies

```bash
git clone https://github.com/yourname/qemu-platform-demo.git

cd qemu-platform-demo

bash scripts/00_install_deps.sh
```

---

# Step 1: Build Linux Kernel (ARM64)

```bash
bash scripts/01_build_kernel.sh
```

Build time:

```text
~10 to 30 minutes
(depending on CPU performance)
```

This step will:

- Download Linux Kernel 6.6.x
- Configure ARM64 kernel
- Build:
  - Image
  - modules
  - DTB files

---

# Step 2: Generate and Patch QEMU DTB

```bash
bash scripts/02_patch_dtb.sh linux-6.6.30
```

This step will:

- Extract QEMU DTB
- Apply custom DTS fragment
- Generate patched DTB

---

# Step 3: Build Out-of-tree Platform Driver

```bash
bash scripts/03_build_driver.sh linux-6.6.30
```

This step builds:

```text
myled_ctrl.ko
```

Kernel module features:

- Platform driver registration
- Device Tree matching
- sysfs interface
- Virtual LED control demo

---

# Step 4: Build Minimal initramfs

## Verify Rootfs Architecture

```bash
qemu-system-arm --version

which busybox

file /usr/bin/busybox
```

---

## Common Problem: Wrong BusyBox Architecture

- Kernel is ARM64
- BusyBox is x86-64
- ARM64 kernel cannot execute `/bin/sh`

As a result:

```bash
#!/bin/sh
```

inside `init` fails immediately.

---

## Fix BusyBox Architecture

Run:

```bash
bash scripts/0A_fix_busybox_arch.sh
```

Then update the BusyBox path inside:

```text
scripts/04_build_rootfs.sh
```

Example:

```bash
BUSYBOX=~/桌面/Linux-kernel/qemu-platform-demo/busybox-1.36.1/busybox
```

---

## Build Rootfs

```bash
bash scripts/04_build_rootfs.sh
```

This step creates:

```text
rootfs/initramfs.cpio.gz
```

---

# Step 5: Launch QEMU Demo

```bash
bash scripts/05_run_qemu.sh

## test sh

./test_myled.sh

## Check Kernel Log

dmesg | grep myled
```

## go back

Ctrl + A

X


This step boots:

- ARM64 Linux kernel
- Custom DTB
- initramfs
- Platform driver

inside QEMU.

---

# Step 6: Clean Environment

```bash
bash scripts/06_clean.sh

bash scripts/06_clean.sh --all

bash scripts/06_clean.sh --dry-run
```

Cleanup Script Usage

| Scenario | Command | Description |
|---|---|---|
| Clean build artifacts during development | `bash cleanup.sh` | Keep kernel source, fastest cleanup |
| Full reset before commit | `bash cleanup.sh --all` | Remove everything and rebuild from scratch |
| Preview what will be removed | `bash cleanup.sh --all --dry-run` | Print only, no deletion |

---

# Project Structure

```text
qemu-platform-demo/
├── docs
│   ├── ......
│
├── driver
│   ├── Makefile
│   ├── myled_ctrl.c
│   └── myled_ctrl.h
│
├── dts
│   ├── myled-fragment.dts
│   └── patch_dtb.sh
│
├── README_qemu.md
│
├── rootfs
│   └── overlay
│       ├── init
│       └── test_myled.sh
│
└── scripts
    ├── 00_install_deps.sh
    ├── 01_build_kernel.sh
    ├── 02_patch_dtb.sh
    ├── 03_build_driver.sh
    ├── 04_build_rootfs.sh
    ├── 05_run_qemu.sh
    ├── 06_clean.sh
    └── 0A_fix_busybox_arch.sh
```

---

---

# QEMU Platform Driver Demo（ARM64）

## 專案概述

本專案示範如何：

- 編譯 ARM64 Linux Kernel
- Patch QEMU Device Tree Blob（DTB）
- 開發 Out-of-tree Platform Driver
- 建立最小 initramfs
- 在 QEMU 啟動自訂 ARM64 Linux 系統
- 驗證 Platform Driver 功能

展示環境包含：

- QEMU ARM64（virt machine）
- Linux Kernel 6.6.x
- BusyBox rootfs
- Device Tree overlay / patch
- Platform Driver + sysfs 操作

---

# 環境建置

## 0. Clone 專案並安裝相依套件

```bash
git clone https://github.com/yourname/qemu-platform-demo.git

cd qemu-platform-demo

bash scripts/00_install_deps.sh
```

---

# Step 1：編譯 Linux Kernel（ARM64）

```bash
bash scripts/01_build_kernel.sh
```

編譯時間：

```text
約 10～30 分鐘
（依 CPU 效能而定）
```

此步驟會：

- 下載 Linux Kernel 6.6.x
- 設定 ARM64 kernel config
- 編譯：
  - Image
  - modules
  - DTB

---

# Step 2：產生並 Patch QEMU DTB

```bash
bash scripts/02_patch_dtb.sh linux-6.6.30
```

此步驟會：

- 擷取 QEMU DTB
- 套用自訂 DTS fragment
- 產生 patch 後的 DTB

---

# Step 3：編譯 Out-of-tree Platform Driver

```bash
bash scripts/03_build_driver.sh linux-6.6.30
```

此步驟會產生：

```text
myled_ctrl.ko
```

Driver 功能包含：

- Platform driver registration
- Device Tree matching
- sysfs 介面
- 虛擬 LED 控制示範

---

# Step 4：建立最小 initramfs

## 確認 Rootfs 架構

```bash
qemu-system-arm --version

which busybox

file /usr/bin/busybox
```

---

## 常見問題：BusyBox 架構錯誤

- kernel 是 ARM64
- BusyBox 是 x86-64
- ARM64 kernel 無法執行 `/bin/sh`

因此：

```bash
#!/bin/sh
```

在 init 腳本中會直接失敗。

---

## 修正 BusyBox 架構

執行：

```bash
bash scripts/0A_fix_busybox_arch.sh
```

接著修改：

```text
scripts/04_build_rootfs.sh
```

中的 BusyBox 路徑。

範例：

```bash
BUSYBOX=~/桌面/Linux-kernel/qemu-platform-demo/busybox-1.36.1/busybox
```

---

## 建立 Rootfs

```bash
bash scripts/04_build_rootfs.sh
```

此步驟會產生：

```text
rootfs/initramfs.cpio.gz
```

---

# Step 5：啟動 QEMU 展示

```bash
bash scripts/05_run_qemu.sh

## 執行測試腳本

./test_myled.sh

## Check Kernel Log

dmesg | grep myled
```

## go back

Ctrl + A

X


此步驟會在 QEMU 中啟動：

- ARM64 Linux kernel
- 自訂 DTB
- initramfs
- Platform driver

---

# Step 6：清理環境

```bash
bash scripts/06_clean.sh

bash scripts/06_clean.sh --all

bash scripts/06_clean.sh --dry-run
```

cleanup.sh 使用方式

| 情境 | 指令 | 說明 |
|---|---|---|
| 開發中清掉 build 產物 | `bash cleanup.sh` | 保留 kernel source，速度最快 |
| 提交前完整重置 | `bash cleanup.sh --all` | 全部刪除，從零重新建立 |
| 先確認會刪除哪些檔案 | `bash cleanup.sh --all --dry-run` | 只列印，不實際刪除 |

---

# 專案結構

```text
qemu-platform-demo/
├── docs
│   ├── ......
│
├── driver
│   ├── Makefile
│   ├── myled_ctrl.c
│   └── myled_ctrl.h
│
├── dts
│   ├── myled-fragment.dts
│   └── patch_dtb.sh
│
├── README_qemu.md
│
├── rootfs
│   └── overlay
│       ├── init
│       └── test_myled.sh
│
└── scripts
    ├── 00_install_deps.sh
    ├── 01_build_kernel.sh
    ├── 02_patch_dtb.sh
    ├── 03_build_driver.sh
    ├── 04_build_rootfs.sh
    ├── 05_run_qemu.sh
    ├── 06_clean.sh
    └── 0A_fix_busybox_arch.sh
```
