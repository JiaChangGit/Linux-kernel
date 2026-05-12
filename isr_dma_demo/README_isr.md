# ISR + DMA Ring Buffer Demo

> A Linux kernel module that uses an `hrtimer` callback as an ISR-like producer, writes fixed-size records into a shared ring buffer, exposes the buffer through a character device, and compares a `read()` path with an intended `mmap()` zero-copy path.

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
![Platform](https://img.shields.io/badge/platform-Ubuntu%2024.04-orange)
![Language](https://img.shields.io/badge/language-C-lightgrey)
![Kernel](https://img.shields.io/badge/kernel%20module-.ko-red)

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Project Structure](#project-structure)
- [Quick Start](#quick-start)
- [Step-by-Step Guide](#step-by-step-guide)
- [How It Works](#how-it-works)
- [Benchmark Results](#benchmark-results)
- [Concepts Explained](#concepts-explained)
- [Requirements](#requirements)
- [中文說明](#中文說明)

---

## Overview

This project demonstrates a specific Linux driver data path:

1. An `hrtimer` callback runs every 500 us and behaves like a fast producer.
2. The producer writes one 64-byte record into a ring buffer.
3. Userspace accesses that data through `/dev/isr_dma`.

The project uses `dma_alloc_coherent()` when available and falls back to `vzalloc()` when DMA-coherent allocation is not available on the current platform. In both cases, the ring buffer is shared between kernel space and userspace through the character device.

The userspace program is designed to compare two access paths:

| Path | Mechanism | Copies | Syscall |
|------|-----------|--------|---------|
| **A: mmap zero-copy** | Shared mapped buffer | 0 after mapping | 0 per slot |
| **B: read() syscall** | `copy_to_user()` per slot | 1 | 1 per slot |

### Current Status

- Module load, `/dev/isr_dma`, `/proc/isr_dma_stats`, and the `read()` data path work.
- The `mmap()` benchmark path is present in the code, but the current `consumer` requests a mapping that is larger than the kernel allows. On the current codebase, `mmap()` returns `EINVAL`.
- Because of that mismatch, the current benchmark does **not** provide a valid end-to-end `mmap()` versus `read()` performance comparison.

---

## Architecture

```
  ┌──────────────────────────────────────────────────────────────┐
  │                      Kernel Space                            │
  │                                                              │
  │   hrtimer (500 µs period)                                   │
  │        │ fires ~2000 callbacks/s                            │
  │        ▼                                                     │
  │   ISR-like producer ─► write 64-byte slot ─► Shared Ring    │
  │                    (timestamp + counter)   [256 slots]       │
  │                                            ▲                 │
  │   /proc/isr_dma_stats ◄── atomic counters ─┘                │
  └───────────────────────────────┬─────────────────────────────┘
                                  │  mmap() / read()
  ┌───────────────────────────────▼─────────────────────────────┐
  │                     Userspace                                │
  │                                                              │
  │   Path A (zero-copy):  tail ptr advance → read from mmap    │
  │   Path B (copy):       read(fd, buf, 64) → copy_to_user     │
  │                                                              │
  │   consumer binary  →  bench_results.txt  →  report          │
  └──────────────────────────────────────────────────────────────┘
```

### Ring Buffer Memory Layout

```
  ┌──────────────────────────────────────────────────────────┐
  │  ring_ctrl  (control block, at base of DMA allocation)  │
  │    head: atomic32   tail: atomic32                       │
  │    isr_count: u64   drop_count: u64                      │
  ├──────────────────────────────────────────────────────────┤
  │  slot[0]  64 bytes  [ts:8][counter:8][0xAB * 48]        │
  │  slot[1]  64 bytes  ...                                  │
  │  ...                                                     │
  │  slot[255] 64 bytes                                      │
  └──────────────────────────────────────────────────────────┘
         ▲ kernel producer and userspace can map the same buffer
```

---

## Project Structure

```
isr_dma_demo/
├── kernel/
│   ├── isr_dma_module.c     # Kernel module: ISR + ring buffer + char dev
│   └── Makefile             # Kernel build system (kbuild)
├── userspace/
│   ├── consumer.c           # Userspace consumer + benchmark
│   └── Makefile             # GCC build
├── scripts/
│   ├── 01_setup.sh          # Install deps, build module & consumer
│   ├── 02_demo.sh           # Load module, live ISR demo
│   ├── 03_benchmark.sh      # Performance comparison
│   ├── 04_cleanup.sh        # Unload module, clean artefacts
│   └── run_all.sh           # One-shot: runs all 4 scripts
└── README_isr.md
```

---

## Quick Start

```bash
# Enter the project directory
cd isr_dma_demo

# Run the full demo in one shot
sudo bash scripts/run_all.sh
```

Or run each step individually (see [Step-by-Step Guide](#step-by-step-guide) below).

---

## Step-by-Step Guide

### Step 0 — Prerequisites

You need Ubuntu 24.04 (or any recent Ubuntu with a 6.x kernel) and the ability to run `sudo`. No cross-compilation or QEMU is needed — everything runs natively on your host.

```bash
# Verify your environment
uname -r        # should print something like 6.8.0-xx-generic
lsb_release -a  # should show Ubuntu 24.04
```

---

### Step 1 — Setup: Install Dependencies and Build

```bash
bash scripts/01_setup.sh
```

This script installs `linux-headers-$(uname -r)`, `build-essential`, and related packages via `apt`. It then compiles the kernel module (`isr_dma_module.ko`) using the kernel build system (kbuild) and compiles the userspace `consumer` binary with GCC.

**Expected output** (last few lines):
```
[OK]    Kernel module built: kernel/isr_dma_module.ko
[OK]    Userspace consumer built: userspace/consumer
```

---

### Step 2 — Demo: Load Module and Observe ISR

```bash
sudo bash scripts/02_demo.sh
```

This script loads the module with `insmod`, waits for `/dev/isr_dma`, opens the device to start the timer callback, polls `/proc/isr_dma_stats` three times, and then reads 5 slots through `read()` plus `hexdump`.

**Expected output** (excerpt):
```
  isr_count       : 1442
  drop_count      : 1187
  ring_head       : 255
  ring_tail       : 0
```

If the consumer does not drain data while the timer is running, `drop_count` will increase. That is the expected behavior for this implementation.

---

### Step 3 — Benchmark: Performance Comparison

```bash
sudo bash scripts/03_benchmark.sh [duration_sec]
# default duration: 5 seconds per path
```

The consumer is intended to run two timed loops:
- **Path A**: `mmap()` the shared buffer and advance `tail` from userspace.
- **Path B**: Call `read()` repeatedly so the kernel copies one 64-byte slot with `copy_to_user()`.

Results are printed as a table and saved to `userspace/bench_results.txt`.

**Important:** in the current codebase, Path A fails with `mmap: Invalid argument` because the userspace mapping request is larger than the buffer size that the kernel exports. Path B still runs.

**Observed output from `docs/DEMO_result.txt`**:
```
mmap: Invalid argument

  Access Path                       Avg latency(ns)     Total ops
  ──────────────────────────────────────────────────────────────
  A) ISR+DMA mmap (zero-copy)                    0  140735203397424
  B) read() syscall (copy)                   14056          3914
```

The `mmap` operation count above is not a real measurement. It is an uninitialized value printed after the failed mapping path. Do not use that number as a benchmark result.

---

### Step 4 — Cleanup

```bash
sudo bash scripts/04_cleanup.sh
```

Unloads the module with `rmmod`, removes `/dev/isr_dma` if it was manually created, and runs `make clean` in both `kernel/` and `userspace/`.

---

## How It Works

### The Producer Path (`hrtimer` callback in kernel)

In a hardware driver, a device would normally trigger an interrupt. This project does not register a real IRQ with `request_irq()`. Instead, it uses an `hrtimer` callback every 500 us to create a repeatable, high-frequency producer path.

Each timer callback calls `isr_produce()`, which:
1. Reads the atomic `head` and `tail` to check for overflow.
2. Writes a 64-byte payload (kernel timestamp + ISR counter) into `data[head * SLOT_SIZE]` of the DMA-coherent buffer.
3. Advances `head` with `smp_store_release()` so the consumer sees a consistent view.

### The Shared Ring Buffer

The module first tries `dma_alloc_coherent()`. If that succeeds, the buffer is suitable for CPU access and for DMA-style sharing semantics through the DMA API. If that allocation fails, the module falls back to `vzalloc()`, which keeps the demo functional but is not a DMA-coherent allocation.

The producer writes:

- 8 bytes: kernel timestamp in nanoseconds
- 8 bytes: ISR counter
- 48 bytes: `0xAB` fill pattern

### Mapping to Userspace with `mmap()`

The driver implements `.mmap` in two different ways:

- If the buffer came from `dma_alloc_coherent()`, it uses `dma_mmap_coherent()`.
- If the buffer came from `vzalloc()`, it uses `remap_vmalloc_range()`.

The goal is zero-copy access after the mapping is established. In the current codebase, the userspace benchmark does not complete that path successfully because it asks to map too many bytes.

### Naive Benchmark Path (for comparison)

The "naive" path uses a separate `vmalloc`'d buffer protected by a spinlock, plus `copy_to_user()` on every `read()` call. This represents the older, simpler approach that most introductory driver examples use.

---

## Benchmark Results

At the moment, only the following benchmark statements are supported by the current repository state:

- The kernel tracks producer-side timing for `isr_produce()` and `naive_produce()` in `/proc/isr_dma_stats`.
- The userspace `read()` benchmark path runs end-to-end.
- The userspace `mmap()` benchmark path currently fails before collecting a valid latency result.

That means the repository currently demonstrates:

- a working producer-side timing comparison inside the kernel
- a working `read()` consumer path
- an intended, but not yet validated, `mmap()` consumer benchmark

---

## Concepts Explained

**ISR (Interrupt Service Routine)** is normally a function registered for a hardware interrupt. In this project, the timing source is an `hrtimer`, so the code demonstrates ISR-style constraints without handling a real device IRQ.

**DMA (Direct Memory Access)** lets a hardware device transfer data to RAM without the CPU copying each byte. This project uses the DMA allocation API, but it does not program a real DMA engine.

**Ring Buffer (Circular Buffer)** uses two atomic indices (`head` for producer, `tail` for consumer). In this project, one timer callback produces and one userspace consumer drains data, so the design fits a single-producer/single-consumer model.

---

## Requirements

- Ubuntu 24.04 (kernel 6.x recommended, tested on 6.8.0-generic)
- `sudo` access (for `insmod`/`rmmod`)
- `build-essential`, `linux-headers-$(uname -r)` (auto-installed by `01_setup.sh`)
- No QEMU, no cross-compiler, no special hardware

---
---

# 中文說明

> 以下內容對應目前程式碼的實際行為，會明確區分「已驗證功能」與「設計目標但尚未跑通的功能」。

---

## 專案簡介

本專案展示一條 Linux driver 常見的資料路徑：

- 核心端用 `hrtimer` 週期性產生事件
- producer 將固定大小資料寫入 ring buffer
- 使用者空間透過 `/dev/isr_dma` 讀取資料
- 額外提供 `/proc/isr_dma_stats` 觀察計數與時間統計

專案名稱中雖然有 `ISR` 與 `DMA`，但要精確理解：

- 這裡的 `ISR` 是用 `hrtimer` 模擬的 ISR-like producer，不是真正用 `request_irq()` 掛上的硬體中斷。
- 這裡的 `DMA` 是使用 DMA allocation API 建立共享 buffer，但沒有真實裝置 DMA engine 在搬資料。

---

## 核心概念說明

### 什麼是 ISR（中斷服務常式）？

當硬體裝置需要通知 CPU 時，通常會發出**硬體中斷（IRQ）**，CPU 再進入 ISR。

ISR 必須非常快（不能睡眠、不能等待鎖），因此典型的設計是：

> ISR 只負責把資料放入環形緩衝區，然後讓背景執行緒或使用者空間去消費。

本專案沒有向核心註冊真實 IRQ，而是用 `hrtimer`（高精度計時器）每 500 微秒觸發一次 callback，藉此模擬「需要快速處理、不可做重工作」的 producer 路徑。它適合教學，但不等於真實硬體 IRQ flow。

### 什麼是 DMA 環形緩衝區？

模組會先嘗試用 `dma_alloc_coherent()` 分配 buffer；若失敗，則 fallback 到 `vzalloc()`。因此這裡不能籠統說成「一定是 DMA-coherent memory」，而要分情況理解：

- 若 `dma_alloc_coherent()` 成功：
  - buffer 來自 DMA API
  - `.mmap` 會走 `dma_mmap_coherent()`
- 若 `dma_alloc_coherent()` 失敗：
  - buffer 來自 `vzalloc()`
  - `.mmap` 會走 `remap_vmalloc_range()`

環形緩衝區使用兩個原子指標：`head`（生產者，由 ISR 更新）和 `tail`（消費者，由使用者空間更新）。因為只有一個生產者、一個消費者，不需要鎖，效能極高。

---

## 環境需求

- Ubuntu 24.04（Linux 核心 6.x，建議 6.8.0-xx-generic）
- 可執行 `sudo`（用於 `insmod`、`rmmod`）
- 不需要 QEMU、不需要交叉編譯、不需要特殊硬體

---

## 專案結構

```
isr_dma_demo/
├── kernel/
│   ├── isr_dma_module.c     # 核心模組：ISR + 環形緩衝區 + 字元裝置
│   └── Makefile             # 核心 kbuild 建構系統
├── userspace/
│   ├── consumer.c           # 使用者空間消費者 + 效能測試
│   └── Makefile             # GCC 建構
├── scripts/
│   ├── 01_setup.sh          # 安裝相依套件、編譯模組與程式
│   ├── 02_demo.sh           # 載入模組、即時展示 ISR 運作
│   ├── 03_benchmark.sh      # 效能比較（mmap vs read）
│   ├── 04_cleanup.sh        # 卸載模組、清理建構檔案
│   └── run_all.sh           # 一鍵執行所有步驟
└── README_isr.md            # 本說明文件
```

---

## 完整建立流程

### 第零步：確認環境

```bash
# 確認核心版本（建議 6.x）
uname -r

# 確認 Ubuntu 版本
lsb_release -a

# 確認可以使用 sudo
sudo whoami  # 應輸出 root
```

### 第一步：安裝相依套件與編譯

```bash
# 不需要 root（sudo 在腳本內部處理）
bash scripts/01_setup.sh
```

腳本會自動執行：
- `sudo apt-get install build-essential linux-headers-$(uname -r) ...`
- 用 kbuild 編譯 `kernel/isr_dma_module.ko`
- 用 GCC 編譯 `userspace/consumer`

**成功輸出（最後幾行）**：
```
[OK]    Kernel module built: kernel/isr_dma_module.ko
[OK]    Userspace consumer built: userspace/consumer
```

---

### 第二步：載入模組 + 即時 Demo

```bash
sudo bash scripts/02_demo.sh
```

腳本會依序執行：

1. 用 `insmod` 載入 `isr_dma_module.ko`
2. 等待 `/dev/isr_dma` 裝置節點出現
3. 開啟裝置，觸發 timer callback 開始週期性寫資料
4. 連續三秒讀取 `/proc/isr_dma_stats`，觀察 `isr_count` 遞增
5. 用 `hexdump` 讀取 5 個環形緩衝區 slot，展示 producer 寫入的原始資料
6. 顯示環形緩衝區架構圖

**觀察重點**：`isr_count` 會持續增加；若沒有同步消費資料，`drop_count` 也會增加。這不是錯誤，而是目前 ring buffer overflow 行為的直接反映。

---

### 第三步：效能比較

```bash
sudo bash scripts/03_benchmark.sh [測試秒數]
# 預設每條路徑測試 5 秒
```

此腳本的設計目標，是比較兩條資料路徑的延遲：

| 路徑 | 機制 | 每次操作 syscall 數 | 資料拷貝次數 |
|------|------|---------------------|-------------|
| **A: mmap 零拷貝** | 共享映射緩衝區 | 0 | 0 |
| **B: read() 系統呼叫** | `copy_to_user()` | 1 | 1 |

但依照目前 `docs/DEMO_result.txt`，`mmap()` 路徑會失敗：

```text
mmap: Invalid argument
```

這表示目前 codebase 的 benchmark 現況是：

- `read()` 路徑可執行
- `mmap()` 路徑設計存在，但尚未形成有效 benchmark 結果
- 螢幕上印出的 `mmap_ops` 大數值是失敗後的未初始化值，不能拿來當性能數據

結果同時儲存至 `userspace/bench_results.txt`，方便後續自動化分析。

---

### 第四步：清理環境

```bash
sudo bash scripts/04_cleanup.sh
```

執行：
- `rmmod isr_dma_module`（卸載核心模組）
- 刪除 `/dev/isr_dma`（若為手動建立）
- `make clean`（清除所有編譯產物）

---

## 一鍵執行所有步驟

```bash
# 一次完成建立 → Demo → 效能比較 → 清理
sudo bash scripts/run_all.sh

# 或指定每個路徑的測試時間（預設 5 秒）
sudo bash scripts/run_all.sh 10
```

---

## 技術細節：理論上為什麼 `mmap()` 會比 `read()` 快？

### `read()` 系統呼叫路徑

每次呼叫 `read()` 讀取一個 64 位元組的 slot，都會發生：

1. 使用者空間觸發 `syscall`，CPU 切換到核心態（trap）
2. 核心驗證使用者空間指標合法性
3. `copy_to_user()` 執行一次 64 位元組的記憶體拷貝
4. 從核心態返回使用者空間（context switch overhead）

每個 slot 的「隱性成本」包含核心入口/出口的暫存器儲存與還原，在現代 x86 上大約 200–500 ns。

### `mmap()` 零拷貝路徑

若 `mmap()` 路徑正確建立完成，之後每次讀取 slot 只需要：

1. 讀取 `head`（一次 `atomic load`，acquire 語意）
2. 讀取 `data[tail * 64]`（普通記憶體讀取）
3. 更新 `tail`（一次 `atomic store`，release 語意）

這條路徑的理論優勢是：每個 slot 不需要再進入一次 kernel，也不需要再做一次 `copy_to_user()`。但這是設計層面的優勢；目前 repository 尚未產生有效的 userspace `mmap()` benchmark 數據。

---

## 效能比較呈現

`03_benchmark.sh` 目前會輸出三種資訊：

1. **文字表格**：顯示使用者空間 benchmark 結果
2. **ASCII 條狀圖**：把輸出數值視覺化
3. **核心端統計**：從 `/proc/isr_dma_stats` 顯示 `isr_produce()` 與 `naive_produce()` 的平均時間

要特別區分兩種速度比較：

- `/proc/isr_dma_stats` 裡的 `speedup_x` 是核心端 `naive_produce()` 與 `isr_produce()` 的平均時間比值
- `consumer` 的 benchmark 則是 userspace `mmap()` 與 `read()` 的存取延遲比較

這兩者不是同一個指標。

---

## 常見問題

**Q: `insmod` 時出現 `Operation not permitted`**
A: 需要 root 或 sudo。另外確認 Secure Boot 是否關閉（部分發行版預設不允許載入未簽署的模組）。

**Q: 找不到 `/dev/isr_dma`**
A: 若 udev 尚未反應，腳本會自動執行 `mknod` 建立裝置節點。也可手動：
```bash
MAJOR=$(grep isr_dma /proc/devices | awk '{print $1}')
sudo mknod /dev/isr_dma c $MAJOR 0
sudo chmod 666 /dev/isr_dma
```

**Q: `dma_alloc_coherent` 失敗，改用 vmalloc**
A: 這代表模組仍可執行 demo，但 buffer 不再是 DMA API 配置出的 coherent allocation。功能上可繼續展示 ring buffer、`read()`、`/proc` 與 `.mmap` fallback path，但不能把它描述成「仍然是同一種 DMA buffer」。

**Q: mmap 路徑和 read 路徑加速比不明顯**
A: 對目前 repository 來說，應先處理 `mmap: Invalid argument`。在 `mmap()` benchmark 修正前，畫面上的 `mmap` 路徑數據不具解釋價值。

---

## 授權

本專案以 **GPL v2** 授權釋出，與 Linux 核心模組授權相容。

---

*Made for learning — feel free to fork, extend, and experiment.*
