# linux-ipc-benchmark

> **Linux IPC deep-dive: Message Queue vs Shared Memory — from kernel module to benchmark**

[![License: GPL-2.0](https://img.shields.io/badge/License-GPL--2.0-blue.svg)](LICENSE)
[![Kernel](https://img.shields.io/badge/Kernel-6.8%2B-orange.svg)](https://kernel.org)
[![Ubuntu](https://img.shields.io/badge/Ubuntu-24.04-E95420.svg)](https://ubuntu.com)
[![Language](https://img.shields.io/badge/Language-C-green.svg)](https://en.wikipedia.org/wiki/C_(programming_language))

Two custom kernel modules (`mq_module.ko` / `shm_module.ko`) implement both IPC mechanisms from scratch, expose them as character devices, and publish per-message latency counters via `/proc`. A userspace benchmark drives both with two POSIX threads and charts the throughput gap.

---

## Table of Contents

- [Why this project?](#why-this-project)
- [Architecture](#architecture)
- [Key concepts](#key-concepts)
- [Project structure](#project-structure)
- [Quick start](#quick-start)
- [Script reference](#script-reference)
- [Benchmark results](#benchmark-results)
- [QEMU / VM guide](#qemu--vm-guide)
- [Internals](#internals)
- [繁體中文說明](#繁體中文說明)

---

## Why this project?

The standard textbook answer is *"shared memory is faster"*. This project shows **why**, at the instruction level:

| | Message Queue | Shared Memory (mmap) |
|---|---|---|
| copies per message | **2** (`copy_from_user` + `copy_to_user`) | **0** (direct page access) |
| syscalls per message | **2** (`write` + `read`) | **0** after `mmap` setup |
| kernel sync primitive | wait-queue + mutex | — (lock-free ring in userspace) |
| backpressure | built-in (blocking `write`) | manual (spin on head/tail) |
| speedup in the bundled example benchmark | 1× (baseline) | **5.63×** (`2,692,462 / 478,399`) |

Both mechanisms are implemented as loadable kernel modules so you can read the exact code path that runs for each message.

---

## Architecture

```
  ┌─────────────────────────────────────────────────────────────────┐
  │                        User Space                               │
  │                                                                 │
  │  mq_demo        shm_demo        benchmark                       │
  │  (producer)     (producer+      (2-thread                       │
  │  (consumer)      consumer       producer/consumer               │
  │                  via mmap)      for all 3 paths)                │
  │                                                                 │
  │  write(fd, msg) ──────────────────────────────────────────────┐ │
  │  read(fd, buf)  ───────────────────────────────────────┐      │ │
  │  mmap(fd, ...)  ──────────────────────────────┐        │      │ │
  └──────────────────────────────────────────────┼────────┼──────┼─┘
  ────────────────────────── syscall boundary ───┼────────┼──────┼──
  ┌──────────────────────────────────────────────┼────────┼──────┼─┐
  │                       Kernel Space           │        │      │ │
  │                                              ▼        ▼      ▼ │
  │  ┌──────────────────────────┐    ┌───────────────────────────┐ │
  │  │      mq_module.ko        │    │       shm_module.ko        │ │
  │  │  /dev/mq_ipc             │    │  /dev/shm_ipc              │ │
  │  │                          │    │                            │ │
  │  │  kfifo (ring, 32 KB)     │    │  vmalloc ring-buffer       │ │
  │  │  mutex + wait-queue      │    │  spinlock  (syscall path)  │ │
  │  │                          │    │  mmap()    (zero-copy path)│ │
  │  │  /proc/mq_stats          │    │  /proc/shm_stats           │ │
  │  └──────────────────────────┘    └───────────────────────────┘ │
  └─────────────────────────────────────────────────────────────────┘

  Data paths
  ──────────
  [MQ  syscall]   write → copy_from_user → kfifo → copy_to_user → read
  [SHM syscall]   write → copy_from_user → ring  → copy_to_user → read
  [SHM mmap  ]   producer writes data[head] directly ──► consumer reads
                  *** zero copy_from/to_user, zero per-message syscall ***
```

---

## Key concepts

### Message Queue (`mq_module.ko`)

Built on `kfifo`, Linux's lock-free kernel FIFO. Two wait-queues handle back-pressure:

- **Full queue** → producer sleeps on `g_wr_wq` until consumer drains a slot.
- **Empty queue** → consumer sleeps on `g_rd_wq` until producer fills a slot.

Every message traverses the user/kernel boundary twice:

```
Producer                          Kernel                       Consumer
   │                                 │                             │
   │── write(fd, msg, 64) ──────────►│                             │
   │                    copy_from_user(kbuf, msg, 64)              │
   │                    kfifo_in(&g_fifo, kbuf, 64)                │
   │                    wake_up(g_rd_wq)                           │
   │                                 │── read(fd, buf, 64) ───────►│
   │                    kfifo_out(&g_fifo, kbuf, 64)               │
   │                    copy_to_user(buf, kbuf, 64)                │
   │                                 │◄────────────── 64 bytes ────│
```

### Shared Memory (`shm_module.ko`)

A `vmalloc()`-allocated ring-buffer is exposed via `mmap()`. After setup, both sides hold a virtual pointer to the **same physical pages**:

```
Producer                       Shared Pages                    Consumer
   │                         ┌─────────────┐                      │
   │  shm = mmap(fd, ...)    │ head / tail │   shm = mmap(fd, .)  │
   │                         │ data[0..N]  │                      │
   │  shm->data[head] = msg ─►  (written)  │                      │
   │  __sync_synchronize()   │             │                      │
   │  shm->head = next       │  (updated)  │                      │
   │                         │             │◄─ msg = shm->data[t] │
   │                         └─────────────┘   shm->tail = next+1│
   │                                                              │
   │◄─── NO copy_from_user / copy_to_user ──────────────────────►│
   │◄─── NO per-message syscall ─────────────────────────────────►│
```

---

## Project structure

```
linux-ipc-benchmark/
├── kernel/
│   ├── mq_module.c     # Message Queue kernel module (kfifo + wait-queue)
│   ├── shm_module.c    # Shared Memory kernel module (vmalloc + mmap)
│   └── Makefile
├── user/
│   ├── common.h        # Shared constants & shm_region_t layout
│   ├── mq_demo.c       # Interactive MQ demo (enqueue / dequeue steps)
│   ├── shm_demo.c      # Interactive SHM mmap demo
│   ├── benchmark.c     # 3-way throughput benchmark (pthreads)
│   └── Makefile
├── scripts/
│   ├── 01_setup.sh     # Install deps → build → insmod → chmod
│   ├── 02_demo.sh      # Step-by-step concept demo
│   ├── 03_benchmark.sh # Throughput benchmark + result chart
│   └── 04_cleanup.sh   # rmmod → make clean → verify
├── Makefile
└── README.md
```

---

## Quick start

### Prerequisites

- Ubuntu 24.04 (bare-metal, VM, or QEMU — see [QEMU / VM guide](#qemu--vm-guide))
- Kernel headers matching the running kernel
- `build-essential`, `kmod`

All of the above are installed automatically by `01_setup.sh`.

### Run everything

```bash
git clone https://github.com/<you>/linux-ipc-benchmark.git
cd linux-ipc-benchmark

# 1. Install deps, build .ko files, load modules
sudo bash scripts/01_setup.sh

# 2. Step-by-step concept demo
sudo bash scripts/02_demo.sh

# 3. Throughput benchmark (default 200 000 messages)
sudo bash scripts/03_benchmark.sh

# 3b. Custom message count
sudo bash scripts/03_benchmark.sh 1000000

# 4. Unload modules, clean build artifacts
sudo bash scripts/04_cleanup.sh
```

### Manual build & load (optional)

```bash
# Build only
make

# Load manually
sudo insmod kernel/mq_module.ko
sudo insmod kernel/shm_module.ko
sudo chmod 666 /dev/mq_ipc /dev/shm_ipc

# Inspect
cat /proc/mq_stats
cat /proc/shm_stats
dmesg | tail -10

# Run individual programs
sudo ./user/mq_demo
sudo ./user/shm_demo
sudo ./user/benchmark 500000

# Unload
sudo rmmod shm_module mq_module
```

---

## Script reference

| Script | What it does |
|--------|--------------|
| `01_setup.sh` | `apt install` headers + tools → `make` kernel + user → `insmod` → `chmod 666` devices |
| `02_demo.sh` | Explains each data path, then runs `mq_demo` and `shm_demo` interactively |
| `03_benchmark.sh` | Runs `./user/benchmark [count]`, prints throughput table + interpretation guide |
| `04_cleanup.sh` | `rmmod` both modules → `make clean` → verifies devices and proc entries are gone |

All scripts must be run as root (`sudo`).

---

## Benchmark results

Example output on a 4-core KVM guest (Intel i7-12700, Ubuntu 24.04, kernel 6.8.0):

```
══════════════════════════════════════════════════════════════
  Linux IPC Benchmark  —  MQ vs Shared Memory
══════════════════════════════════════════════════════════════
  msg count : 200000
  msg size  : 64 bytes
  data vol  : 12.2 MB
  ring cap  : 512 slots

──────────────────────────────────────────────────────────────
[1] Message Queue  (kfifo + blocking syscall)
    copy path: write→copy_from_user→kfifo→copy_to_user→read

    producer :    412.3 ms  →     485,388 msg/s
    consumer :    389.6 ms  →     513,356 msg/s
    wall     :    418.1 ms  →     478,399 msg/s  (combined)

──────────────────────────────────────────────────────────────
[2] Shared Memory  (ring-buf + syscall, spinlock)
    copy path: write→copy_from_user→ring→copy_to_user→read

    producer :    298.7 ms  →     669,716 msg/s
    consumer :    281.4 ms  →     710,735 msg/s
    wall     :    305.2 ms  →     655,341 msg/s  (combined)

──────────────────────────────────────────────────────────────
[3] Shared Memory  (ring-buf + mmap, ZERO-COPY)
    copy path: producer memcpy→page  consumer memcpy←page
               *** no copy_from/to_user, no per-msg syscall ***

    producer :     71.4 ms  →   2,801,120 msg/s
    consumer :     68.9 ms  →   2,903,338 msg/s
    wall     :     74.3 ms  →   2,692,462 msg/s  (combined)

──────────────────────────────────────────────────────────────
  SUMMARY  (baseline = MQ throughput)

  [1] MQ  kfifo+syscall    478,399 msg/s  [######--------------------]  x1.00
  [2] SHM ring+syscall     655,341 msg/s  [########------------------]  x1.37
  [3] SHM ring+mmap      2,692,462 msg/s  [############################]  x5.63
```

In the example run above, the mmap path reaches **2,692,462 msg/s**, which is **5.63×** the MQ baseline of **478,399 msg/s**. In this project, that difference comes from removing `copy_from_user` / `copy_to_user` and the two per-message syscalls from the data path.

---

## QEMU / VM guide

The following QEMU workflow targets an x86-64 Ubuntu 24.04 guest:

```bash
# Install QEMU on the host
sudo apt install qemu-system-x86 cloud-image-utils

# Download Ubuntu 24.04 cloud image
wget https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img

# Create a writable overlay (so the base image stays clean)
qemu-img create -f qcow2 -b noble-server-cloudimg-amd64.img -F qcow2 ipc-test.qcow2 20G

# Build a cloud-init seed (sets password for 'ubuntu' user)
cat > user-data.yaml <<'EOF'
#cloud-config
password: ubuntu
chpasswd: {expire: false}
ssh_pwauth: true
EOF
cloud-localds seed.iso user-data.yaml

# Boot with 2 vCPUs and 2 GiB RAM
qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 \
  -smp 2 \
  -drive file=ipc-test.qcow2,format=qcow2 \
  -drive file=seed.iso,format=raw \
  -net nic -net user,hostfwd=tcp::2222-:22 \
  -nographic

# SSH in from the host
ssh -p 2222 ubuntu@localhost
# password: ubuntu
```

Once inside the VM, clone the repository and run the scripts. `-enable-kvm` requires hardware virtualisation support on the host. If KVM is unavailable, remove `-enable-kvm`; module behaviour stays the same, but benchmark throughput numbers will change because the CPU execution environment is different.

---

## Internals

### Why `vmalloc` for shared memory?

`kmalloc` requires physically contiguous memory. `vmalloc` provides a virtually contiguous region backed by page-sized physical allocations, which is why this project can map the shared region page-by-page with `vmalloc_to_pfn()`. In this codebase, the shared region size is `PAGE_ALIGN(sizeof(struct shm_region))`, and the ring payload itself is `512 × 64 = 32,768` bytes.

### Ubuntu 24 / kernel 6.8 API changes

| API | Change | This project uses |
|-----|--------|-------------------|
| `class_create` | `(owner, name)` → `(name)` since 6.4 | `class_create(NAME "_class")` |
| `vma->vm_flags` | became `const` since 6.3 | `vm_flags_set(vma, flags)` |
| `/proc` ops | `file_operations` → `proc_ops` since 5.6 | `struct proc_ops` |

### Lock-free ring buffer (mmap path)

The single-producer / single-consumer ring is lock-free because each side owns exactly one index:

- **Producer** writes `head` — consumer only reads `head`.
- **Consumer** writes `tail` — producer only reads `tail`.
- `__sync_synchronize()` / `smp_wmb()` / `smp_rmb()` ensure ordering.

No mutex. No atomic CAS. In the bundled example benchmark, this path reaches `2,692,462 msg/s`, compared with `655,341 msg/s` for the SHM syscall path and `478,399 msg/s` for MQ.

---

---

## 繁體中文說明

### 專案簡介

本專案透過兩個 Linux 核心模組（`.ko` 檔），從零實作 **Message Queue** 與 **Shared Memory** 兩種行程間通訊（IPC）機制，並以具備雙執行緒的使用者空間 benchmark 進行吞吐量比較。核心重點在於讓人清楚看見：**每條訊息究竟在哪一層被複製了幾次、進出核心空間幾次**。

### 核心觀念

**Message Queue（`mq_module.ko`）**

資料路徑：
```
[Producer]  write() → copy_from_user → kfifo → copy_to_user → read()  [Consumer]
                           ↑ 第 1 次複製                 ↑ 第 2 次複製
```
- 每條訊息跨越 user/kernel 邊界 **兩次**
- 以 `kfifo` 實作 kernel 側環形緩衝
- `mutex` + `wait_queue` 處理生產者滿、消費者空的阻塞

**Shared Memory（`shm_module.ko`）**

```
[Producer] ──► shm->data[head] = msg   ← 直接寫入 mmap 的共享頁面
               __sync_synchronize()
               shm->head = next

[Consumer] ←── msg = shm->data[tail]   ← 直接讀取同一塊物理頁面
               shm->tail = next
```
- `mmap()` 之後，producer 和 consumer 操作的是**同一批物理頁面**
- 零 `copy_from_user` / `copy_to_user`
- 零每訊息 syscall
- 以 SPSC（Single-Producer Single-Consumer）無鎖環形佇列同步

### 專案結構

```
linux-ipc-benchmark/
├── kernel/
│   ├── mq_module.c     # MQ 核心模組（kfifo + wait-queue）
│   ├── shm_module.c    # SHM 核心模組（vmalloc ring-buf + mmap）
│   └── Makefile
├── user/
│   ├── common.h        # 共用常數與 shm_region_t 定義
│   ├── mq_demo.c       # MQ 互動展示
│   ├── shm_demo.c      # SHM mmap 互動展示
│   ├── benchmark.c     # 三路吞吐量 benchmark（pthread）
│   └── Makefile
├── scripts/
│   ├── 01_setup.sh     # 安裝相依套件→編譯→載入模組
│   ├── 02_demo.sh      # 概念演示
│   ├── 03_benchmark.sh # 效能比較
│   └── 04_cleanup.sh   # 卸載模組→清除編譯產物
├── Makefile
└── README.md
```

### 環境需求

- **OS**：Ubuntu 24.04（實體機、VM、或 QEMU x86-64）
- **Kernel**：6.8.x（Ubuntu 24.04 預設）
- **套件**：`build-essential`、`linux-headers-$(uname -r)`、`kmod`（`01_setup.sh` 自動安裝）

### 快速開始

```bash
git clone https://github.com/<你的帳號>/linux-ipc-benchmark.git
cd linux-ipc-benchmark

# 步驟 1：安裝套件、編譯 .ko 與使用者程式、載入模組
sudo bash scripts/01_setup.sh

# 步驟 2：逐步概念演示（顯示每條訊息的複製時間）
sudo bash scripts/02_demo.sh

# 步驟 3：效能 benchmark（預設 200,000 條訊息）
sudo bash scripts/03_benchmark.sh

# 步驟 3b：自訂訊息數量
sudo bash scripts/03_benchmark.sh 1000000

# 步驟 4：環境清理（卸載模組 + 刪除編譯產物）
sudo bash scripts/04_cleanup.sh
```

### QEMU 環境建立

```bash
# 在 host 安裝 QEMU
sudo apt install qemu-system-x86 cloud-image-utils

# 下載 Ubuntu 24.04 cloud image
wget https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img

# 建立可寫入的 overlay（保留原始 image 乾淨）
qemu-img create -f qcow2 \
  -b noble-server-cloudimg-amd64.img -F qcow2 ipc-test.qcow2 20G

# 建立 cloud-init seed（設定密碼）
cat > user-data.yaml <<'EOF'
#cloud-config
password: ubuntu
chpasswd: {expire: false}
ssh_pwauth: true
EOF
cloud-localds seed.iso user-data.yaml

# 啟動 VM（2 vCPU、2 GB RAM）
qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 \
  -smp 2 \
  -drive file=ipc-test.qcow2,format=qcow2 \
  -drive file=seed.iso,format=raw \
  -net nic -net user,hostfwd=tcp::2222-:22 \
  -nographic

# 從 host SSH 進入（密碼：ubuntu）
ssh -p 2222 ubuntu@localhost
```

> 若宿主機不支援 KVM，移除 `-enable-kvm` 即可；模組行為不變，但 benchmark 吞吐量數字會因執行環境改變而不同。

### 腳本說明

| 腳本 | 功能 |
|------|------|
| `01_setup.sh` | `apt install` 編譯工具 → `make` → `insmod` → `chmod 666` 裝置節點 |
| `02_demo.sh` | 說明各路徑原理，逐步執行 `mq_demo` 與 `shm_demo` |
| `03_benchmark.sh` | 執行 `benchmark [count]`，輸出吞吐量表格與解讀說明 |
| `04_cleanup.sh` | `rmmod` → `make clean` → 驗證裝置節點與 proc 項目已消失 |

所有腳本需以 `sudo` 執行。

### Benchmark 解讀

```
  [1] MQ  kfifo+syscall   ~480,000 msg/s   x1.00（基準線）
  [2] SHM ring+syscall    ~655,000 msg/s   x1.37（省去 wait-queue 排程開銷）
  [3] SHM ring+mmap     ~2,700,000 msg/s   x5.6 （零複製、零每訊息 syscall）
```

**[1] vs [2]**：相同複製次數，差異來自佇列機制（wait-queue vs spinlock）。

**[2] vs [3]**：這是核心對比。mmap 路徑消除了：
- `copy_from_user` / `copy_to_user` 的 CPU 時間與快取污染
- 每條訊息的 syscall mode-switch 成本

**選用時機**：
- **Message Queue**：生產者/消費者解耦、需要核心管理生命週期、中等吞吐量
- **Shared Memory**：極高吞吐量、低延遲、可接受緊耦合、需自行管理同步

### 核心技術細節

**為什麼用 `vmalloc`？**
`kmalloc` 要求物理連續記憶體，實際上限約 4 MB。`vmalloc` 不要求連續物理頁面，更適合大型緩衝區。代價是 `mmap` 時需逐頁呼叫 `vmalloc_to_pfn()` + `remap_pfn_range()`。

**Ubuntu 24 / Kernel 6.8 API 差異**

| API | 變更 | 本專案處理方式 |
|-----|------|--------------|
| `class_create` | `(owner, name)` → `(name)`（≥ 6.4）| `class_create(NAME "_class")` |
| `vma->vm_flags` | 改為 `const`（≥ 6.3）| `vm_flags_set(vma, flags)` |
| `/proc` ops | `file_operations` → `proc_ops`（≥ 5.6）| `struct proc_ops` |

---

## License

GPL-2.0 — see [LICENSE](LICENSE).
