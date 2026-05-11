# Linux Kernel Portfolio Projects

This repository is a curated portfolio of three Linux system software projects, designed to demonstrate practical engineering ability across user space, kernel space, and embedded platform bring-up.

It is organized as a progression:

- `fwsh`: user-space shell and process control
- `chardev-driver`: Linux kernel character device driver development
- `qemu-platform-demo`: ARM64 embedded Linux platform-driver workflow in QEMU

These projects are intended to show hands-on experience with:

- C system programming
- POSIX process and signal handling
- Linux kernel module development
- `procfs`, `sysfs`, and `ioctl` interfaces
- cross-compilation for ARM64
- Device Tree integration
- initramfs construction
- QEMU-based platform validation

## Key Skills

`C` `Linux` `Linux Kernel` `Kernel Module` `Character Device Driver` `POSIX` `fork/exec` `pipe` `signal`
`procfs` `sysfs` `ioctl` `Device Tree` `ARM64` `Cross Compilation` `QEMU` `BusyBox` `initramfs` `Makefile`

## Portfolio Overview

| Project | Domain | What It Demonstrates | Entry |
|---|---|---|---|
| `fwsh` | User-space systems programming | `fork/exec`, pipelines, redirection, built-ins, signal handling, memory discipline | [fwsh/README_fwsh.md](fwsh/README_fwsh.md) |
| `chardev-driver` | Linux kernel module development | character device registration, userspace-kernel interface design, `procfs`, `sysfs`, `ioctl` | [chardev-driver/README_char.md](chardev-driver/README_char.md) |
| `qemu-platform-demo` | Embedded Linux / ARM64 | kernel build flow, DTB patching, out-of-tree driver integration, initramfs, QEMU boot and verification | [qemu-platform-demo/README_qemu.md](qemu-platform-demo/README_qemu.md) |

- `fwsh` shows command parsing, process creation, file descriptor handling, and interactive shell behavior.
- `chardev-driver` moves into kernel-space development and exposes a driver through multiple Linux interfaces.
- `qemu-platform-demo` extends the work into a fuller embedded workflow, including toolchain setup, kernel build, driver deployment, and virtual platform validation.

Together, they represent the ability to move across software layers and debug Linux systems from application level down to kernel integration.

## Recommended Test Platform

The projects are most suitable for the following host environment:

- OS: Ubuntu 22.04 LTS or Ubuntu 24.04 LTS
- Architecture: `x86_64` Linux host
- Compiler: GCC with C11 support
- Build tools: `make`, `git`, `wget`
- Privileges: `sudo` access for package install, module loading, and selected test steps

Recommended project-specific platform notes:

| Project | Recommended Host | Notes |
|---|---|---|
| `fwsh` | Ubuntu 22.04 / 24.04 | Best for native build and terminal interaction |
| `chardev-driver` | Ubuntu 22.04 on bare metal or VM | Requires matching `linux-headers-$(uname -r)` for the running kernel |
| `qemu-platform-demo` | Ubuntu 22.04 / 24.04 with enough disk and CPU | Cross-builds Linux `6.6.30` for `ARM64` and runs it in QEMU |

## System Requirements

| Project | Required Tools / Packages | Notes |
|---|---|---|
| `fwsh` | `gcc`, `make`, `libreadline-dev` | Native C11 user-space program |
| `chardev-driver` | `build-essential`, `linux-headers-$(uname -r)`, `kmod`, `gcc`, `make` | Module insertion and device-node access require `sudo` |
| `qemu-platform-demo` | `gcc-aarch64-linux-gnu`, `g++-aarch64-linux-gnu`, `qemu-system-arm`, `device-tree-compiler`, `libfdt-dev`, `busybox-static`, `cpio`, `bc`, `bison`, `flex`, `libssl-dev`, `libelf-dev`, `make`, `git`, `wget` | Uses `ARCH=arm64` and `CROSS_COMPILE=aarch64-linux-gnu-` |

Additional environment notes:

- `chardev-driver` depends on `/lib/modules/$(uname -r)/build` being present.
- `qemu-platform-demo` downloads and builds Linux kernel `6.6.30`.
- `qemu-platform-demo` requires a usable ARM64 BusyBox binary when generating `initramfs`.

## Suggested Reading Order

1. `fwsh`
2. `chardev-driver`
3. `qemu-platform-demo`

## Repository Layout

```text
Linux-kernel/
├── fwsh/
├── chardev-driver/
├── qemu-platform-demo/
├── LICENSE
└── README.md
```

## Quick Links

- [fwsh](fwsh/)
- [chardev-driver](chardev-driver/)
- [qemu-platform-demo](qemu-platform-demo/)

---

# Linux Kernel 求職作品集專案

這個 repository 是一組整理過的 Linux 系統軟體作品集，目的是展示我在 user space、kernel space，以及 embedded Linux 平台 bring-up 方面的實作能力。

整體設計是有順序的：

- `fwsh`：從使用者空間的 shell 與行程控制開始
- `chardev-driver`：進入 Linux kernel 字元裝置驅動開發
- `qemu-platform-demo`：延伸到 ARM64 嵌入式 Linux 平台驅動與 QEMU 驗證流程

這三個專案共同呈現的能力包含：

- C 系統程式設計
- POSIX 行程與 signal 處理
- Linux kernel module 開發
- `procfs`、`sysfs`、`ioctl` 介面設計
- ARM64 cross-compilation
- Device Tree 整合
- initramfs 建置
- 使用 QEMU 進行平台驗證

## 核心技能標籤

`C` `Linux` `Linux Kernel` `Kernel Module` `Character Device Driver` `POSIX` `fork/exec` `pipe` `signal`
`procfs` `sysfs` `ioctl` `Device Tree` `ARM64` `Cross Compilation` `QEMU` `BusyBox` `initramfs` `Makefile`

## 作品集總覽

| 專案 | 領域 | 展示能力 | 入口 |
|---|---|---|---|
| `fwsh` | 使用者空間系統程式 | `fork/exec`、pipeline、redirect、built-in commands、signal handling、記憶體管理 | [fwsh/README_fwsh.md](fwsh/README_fwsh.md) |
| `chardev-driver` | Linux kernel module 開發 | character device 註冊、user-kernel 介面設計、`procfs`、`sysfs`、`ioctl` | [chardev-driver/README_char.md](chardev-driver/README_char.md) |
| `qemu-platform-demo` | Embedded Linux / ARM64 | kernel 編譯流程、DTB patch、out-of-tree driver 整合、initramfs、QEMU 開機與驗證 | [qemu-platform-demo/README_qemu.md](qemu-platform-demo/README_qemu.md) |

- `fwsh` 展示指令解析、行程建立、file descriptor 控制，以及互動式 shell 行為。
- `chardev-driver` 進一步進入 kernel-space，透過多種 Linux 介面把驅動功能暴露給 userspace。
- `qemu-platform-demo` 則把範圍擴大到較完整的 embedded Linux workflow，包含 toolchain、kernel、driver、root filesystem 與虛擬平台驗證。

放在一起看，這組作品能反映我具備跨層處理 Linux 系統問題的能力，從應用層一路往下做到 kernel 整合。

## 建議測試平台

這些專案最適合在以下主機環境中操作：

- 作業系統：Ubuntu 22.04 LTS 或 Ubuntu 24.04 LTS
- 主機架構：`x86_64` Linux host
- 編譯器：支援 C11 的 GCC
- 建置工具：`make`、`git`、`wget`
- 權限需求：需要 `sudo` 來安裝套件、載入模組，以及執行部分測試步驟

各專案建議平台補充如下：

| 專案 | 建議平台 | 補充說明 |
|---|---|---|
| `fwsh` | Ubuntu 22.04 / 24.04 | 適合直接在本機終端機進行 native build 與互動測試 |
| `chardev-driver` | Ubuntu 22.04 實機或虛擬機 | 需要與目前執行中 kernel 相符的 `linux-headers-$(uname -r)` |
| `qemu-platform-demo` | Ubuntu 22.04 / 24.04，且具備足夠磁碟與 CPU 資源 | 會 cross-build `ARM64` 的 Linux `6.6.30`，並在 QEMU 中啟動 |

## 系統需求

| 專案 | 需要的工具 / 套件 | 補充說明 |
|---|---|---|
| `fwsh` | `gcc`、`make`、`libreadline-dev` | native 的 C11 user-space 程式 |
| `chardev-driver` | `build-essential`、`linux-headers-$(uname -r)`、`kmod`、`gcc`、`make` | 載入模組與存取 device node 需要 `sudo` |
| `qemu-platform-demo` | `gcc-aarch64-linux-gnu`、`g++-aarch64-linux-gnu`、`qemu-system-arm`、`device-tree-compiler`、`libfdt-dev`、`busybox-static`、`cpio`、`bc`、`bison`、`flex`、`libssl-dev`、`libelf-dev`、`make`、`git`、`wget` | 使用 `ARCH=arm64` 與 `CROSS_COMPILE=aarch64-linux-gnu-` |

額外環境資訊：

- `chardev-driver` 依賴 `/lib/modules/$(uname -r)/build` 必須存在。
- `qemu-platform-demo` 會下載並編譯 Linux kernel `6.6.30`。
- `qemu-platform-demo` 在建立 `initramfs` 時，需要可用的 ARM64 BusyBox 執行檔。

## 建議閱讀順序

1. `fwsh`
2. `chardev-driver`
3. `qemu-platform-demo`

## Repository 結構

```text
Linux-kernel/
├── fwsh/
├── chardev-driver/
├── qemu-platform-demo/
├── LICENSE
└── README.md
```

## 快速入口

- [fwsh](fwsh/)
- [chardev-driver](chardev-driver/)
- [qemu-platform-demo](qemu-platform-demo/)
