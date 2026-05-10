# Linux System Programming & Embedded Demo Collection

> A collection of Linux system programming and embedded Linux projects focused on:
>
> - Linux kernel development
> - Device drivers
> - QEMU virtualization
> - Firmware-oriented tooling
> - POSIX system programming

This repository demonstrates practical low-level engineering skills commonly used in:

- Embedded Linux
- Firmware engineering
- Linux kernel development
- System software development
- Platform bring-up and debugging

---

# Projects Overview

## 1. Character Device Driver

A Linux character device driver demonstrating:

- `cdev` registration
- `ioctl`
- `procfs`
- `sysfs`
- kernel/user-space interaction

### Features

- Read/write device interface
- Runtime statistics
- Read-only mode switching
- procfs status monitoring
- sysfs attribute control

### Tech Stack

- Linux Kernel Module (LKM)
- Character Device Driver
- POSIX userspace testing tools

---

## 2. QEMU Platform Driver Demo

An ARM64 embedded Linux demo running inside QEMU.

Demonstrates:

- ARM64 Linux kernel build
- Device Tree patching
- Platform driver development
- initramfs construction
- QEMU boot flow

### Features

- Out-of-tree platform driver
- DTB patch workflow
- BusyBox rootfs
- sysfs interaction
- Embedded Linux bring-up pipeline

### Tech Stack

- QEMU ARM64
- Linux Kernel 6.x
- Device Tree
- BusyBox
- Platform Driver

---

## 3. fwsh — Firmware Mini Shell

A lightweight shell built from a firmware engineer’s perspective.

Demonstrates advanced Linux userspace programming:

- REPL shell design
- process control
- pipeline execution
- signal handling
- low-level utilities

### Features

- GNU Readline support
- Pipeline + redirection
- Background execution
- Hex dump utility
- CRC32 calculator
- Physical memory map viewer

### Tech Stack

- C
- POSIX API
- fork/exec/pipe/dup2
- signal handling
- GNU Readline

---

# Repository Structure

```text
projects/
├── chardev-driver/
│   └── README_char.md
│
├── qemu-platform-demo/
│   └── README_qemu.md
│
├── fwsh/
│   └── README_fwsh.md
│
└── README.md
```

---

# Skills Demonstrated

## Linux Kernel Development

- Kernel module development
- Character device drivers
- Platform drivers
- procfs/sysfs integration
- ioctl interface design

---

## Embedded Linux

- ARM64 kernel build
- QEMU virtualization
- initramfs construction
- Device Tree workflow
- BusyBox integration

---

## Linux System Programming

- fork/exec model
- process lifecycle management
- pipe and fd management
- signal handling
- interactive shell implementation

---

# Development Environment

## Host Environment

| Component | Version |
|---|---|
| OS | Ubuntu 24.04 |
| GCC | 13.3.0 |
| QEMU | 8.2.2 |

---

## Current Toolchain Information

### QEMU

```bash
qemu-system-arm --version
```

Output:

```text
QEMU emulator version 8.2.2
(Debian 1:8.2.2+ds-0ubuntu1.16)
```

---

### GCC

```bash
gcc --version
```

Output:

```text
gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
```

---

### BusyBox

```bash
file /usr/bin/busybox
```

Output:

```text
ELF 64-bit LSB executable, x86-64
```

---

# Important ARM64 BusyBox Note

The default host BusyBox is:

```text
x86-64
```

However, the QEMU demo environment uses:

```text
ARM64 Linux kernel
```

Therefore:

- ARM64 kernel cannot execute x86-64 BusyBox
- `/bin/sh` inside initramfs will fail
- init process may panic during boot

---

---

# Linux 系統程式與 Embedded Demo 專案集

> 一系列 Linux 系統程式與 Embedded Linux 專案，專注於：
>
> - Linux kernel 開發
> - Device Driver
> - QEMU 虛擬化
> - 韌體工程工具
> - POSIX 系統程式設計

本 repository 展示實務上的 low-level engineering 能力，常見於：

- Embedded Linux
- 韌體工程
- Linux kernel 開發
- 系統軟體開發
- 平台 bring-up 與除錯

---

# 專案介紹

## 1. 字元裝置驅動（Character Device Driver）

一個 Linux 字元裝置驅動專案，展示：

- `cdev` 註冊
- `ioctl`
- `procfs`
- `sysfs`
- kernel/user-space 互動

### 功能特色

- Read/write device 介面
- Runtime 統計資訊
- 唯讀模式切換
- procfs 狀態監控
- sysfs 屬性控制

### 技術重點

- Linux Kernel Module（LKM）
- Character Device Driver
- POSIX userspace 測試工具

---

## 2. QEMU Platform Driver Demo

一個運行於 QEMU ARM64 的 Embedded Linux 展示專案。

展示內容包含：

- ARM64 Linux kernel 編譯
- Device Tree patch
- Platform driver 開發
- initramfs 建立
- QEMU boot flow

### 功能特色

- Out-of-tree platform driver
- DTB patch workflow
- BusyBox rootfs
- sysfs 操作
- Embedded Linux bring-up 流程

### 技術重點

- QEMU ARM64
- Linux Kernel 6.x
- Device Tree
- BusyBox
- Platform Driver

---

## 3. fwsh — 韌體工程師迷你 Shell

一個以韌體工程師角度設計的輕量 Shell。

展示進階 Linux userspace programming 技術：

- REPL shell 設計
- process control
- pipeline execution
- signal handling
- low-level utility 開發

### 功能特色

- GNU Readline 支援
- Pipeline 與 redirect
- Background execution
- Hex dump 工具
- CRC32 計算器
- 實體記憶體配置檢視工具

### 技術重點

- C
- POSIX API
- fork/exec/pipe/dup2
- signal handling
- GNU Readline

---

# Repository 結構

```text
projects/
├── chardev-driver/
│   └── README_char.md
│
├── qemu-platform-demo/
│   └── README_qemu.md
│
├── fwsh/
│   └── README_fwsh.md
│
└── README.md
```

---

# 展示技能

## Linux Kernel 開發

- Kernel module 開發
- Character device driver
- Platform driver
- procfs/sysfs 整合
- ioctl 介面設計

---

## Embedded Linux

- ARM64 kernel 編譯
- QEMU 虛擬化
- initramfs 建立
- Device Tree workflow
- BusyBox 整合

---

## Linux System Programming

- fork/exec 模型
- process lifecycle 管理
- pipe 與 fd 管理
- signal handling
- interactive shell 實作

---

# 開發環境

## Host 環境

| 元件 | 版本 |
|---|---|
| 作業系統 | Ubuntu 24.04 |
| GCC | 13.3.0 |
| QEMU | 8.2.2 |

---

## 目前工具鏈資訊

### QEMU

```bash
qemu-system-arm --version
```

輸出：

```text
QEMU emulator version 8.2.2
(Debian 1:8.2.2+ds-0ubuntu1.16)
```

---

### GCC

```bash
gcc --version
```

輸出：

```text
gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
```

---

### BusyBox

```bash
file /usr/bin/busybox
```

輸出：

```text
ELF 64-bit LSB executable, x86-64
```

---

# ARM64 BusyBox 重要說明

目前 host 系統的 BusyBox 是：

```text
x86-64
```

但 QEMU Demo 使用的是：

```text
ARM64 Linux kernel
```

因此：

- ARM64 kernel 無法執行 x86-64 BusyBox
- initramfs 中的 `/bin/sh` 會失敗
- init process 可能 boot panic

---
