# Linux IPC Benchmark: Message Queue vs Shared Memory

[![Kernel Version](https://img.shields.io/badge/Kernel-6.8%2B-orange.svg)](https://kernel.org)
[![Platform](https://img.shields.io/badge/Platform-Ubuntu%2024.04-blue.svg)](https://ubuntu.com)
[![License](https://img.shields.io/badge/License-GPL--2.0-green.svg)](../LICENSE)

這個專案用兩個 Linux Kernel Module 實作行程間通訊（Inter-Process Communication, IPC），並用同一組 benchmark 比較三種資料傳遞路徑：

1. 訊息佇列（Message Queue, MQ）：`/dev/mq_ipc`
2. 共享記憶體 syscall 路徑（Shared Memory via `read()` / `write()`）：`/dev/shm_ipc`
3. 共享記憶體 `mmap` 路徑（Shared Memory via `mmap()`）：`/dev/shm_ipc`

專案重點不是直接使用 POSIX message queue 或 System V shared memory，而是自己寫一個最小可跑的核心模組，觀察「資料複製次數」、「系統呼叫成本」和「同步方式」如何影響 IPC 效能。

---

## 1. 先看結論

每筆訊息固定為 64 bytes。三種測試路徑的差別如下：

| 測試 | 入口 | 每筆訊息是否進核心 | 使用者/核心資料複製 | 主要同步方式 | 用途 |
| --- | --- | --- | --- | --- | --- |
| MQ | `write()` / `read()` on `/dev/mq_ipc` | 是 | 2 次 | `mutex` + `wait_queue` | 觀察傳統 syscall IPC 的成本 |
| SHM syscall | `write()` / `read()` on `/dev/shm_ipc` | 是 | 2 次 | `spinlock` | 對照「同樣複製次數，不同佇列結構」 |
| SHM mmap | `mmap()` 後直接讀寫 shared region | 只有初始化會進核心 | 0 次 `copy_from_user()` / `copy_to_user()` | 使用者空間輪詢 + memory barrier | 觀察 zero-copy 的成本差異 |

這份 benchmark 不保證固定快幾倍，因為結果會受 CPU、核心版本、排程、背景負載影響。它真正要回答的是：快的原因來自哪裡。

```text
MQ / SHM syscall:
  user buffer
    -> syscall
    -> kernel copy
    -> kernel queue or ring
    -> kernel copy
    -> user buffer

SHM mmap:
  user execution path A
    -> shared mapped pages
    -> user execution path B
```

---

## 2. 關鍵字速查

| 中文 | English | 說明 |
| --- | --- | --- |
| 行程間通訊 | IPC, Inter-Process Communication | 不同行程交換資料的方法，例如 pipe、socket、message queue、shared memory。 |
| 核心模組 | Kernel Module | 可動態載入 Linux kernel 的程式。本專案會產生 `mq_module.ko` 與 `shm_module.ko`。 |
| 字元裝置 | Character Device | 讓使用者程式用 `open()`、`read()`、`write()` 操作核心模組的介面，例如 `/dev/mq_ipc`。 |
| 系統呼叫 | System Call, syscall | 使用者程式進入核心的正式入口，例如 `read()`、`write()`、`mmap()`。 |
| 使用者空間 | User Space | 一般程式執行的位置。不能直接存取核心記憶體。 |
| 核心空間 | Kernel Space | Linux kernel 與 kernel module 執行的位置。 |
| 訊息佇列 | Message Queue | 以 FIFO 順序傳遞一筆一筆訊息。 |
| 共享記憶體 | Shared Memory | 多個執行路徑看到同一段記憶體，資料不必透過核心轉送。 |
| 記憶體映射 | `mmap`, Memory Mapping | 把檔案或裝置背後的記憶體映射到使用者行程的虛擬位址空間。 |
| 零複製 | Zero-copy | 避免在 user/kernel 邊界重複搬移資料。本專案指的是避開 `copy_from_user()` 與 `copy_to_user()`。 |
| 環形緩衝區 | Ring Buffer | 用 `head` 與 `tail` 表示下一個寫入與讀取位置的固定大小佇列。 |
| 快取偽共享 | False Sharing | 兩個 CPU 修改同一條 cache line 上不同欄位，造成快取不斷失效。 |
| 記憶體屏障 | Memory Barrier | 限制 CPU 或編譯器重排記憶體操作，避免先更新索引、後寫資料這類錯誤順序。 |

---

## 3. 專案架構

```text
linux-ipc-benchmark/
├── kernel/
│   ├── mq_module.c      # /dev/mq_ipc: kfifo + mutex + wait queue
│   ├── shm_module.c     # /dev/shm_ipc: vmalloc ring + syscall + mmap
│   └── Makefile
├── user/
│   ├── benchmark.c      # 三種 IPC 路徑的吞吐量測試
│   ├── mq_demo.c        # 小量訊息佇列示範
│   ├── shm_demo.c       # mmap shared memory 示範
│   ├── common.h         # user space 常數與 shared memory layout
│   └── Makefile
├── scripts/
│   ├── 01_setup.sh      # 安裝依賴、建置、載入 kernel modules
│   ├── 02_demo.sh       # 逐步展示 MQ 與 SHM mmap
│   ├── 03_benchmark.sh  # 執行吞吐量測試
│   └── 04_cleanup.sh    # 卸載 modules 並清理 build artifacts
├── docs/                # 執行截圖
├── report_ipc.md        # 設計與實作報告
└── report_ipc_api.md    # API 與架構分析報告
```

---

## 4. 環境需求

建議環境：

- Ubuntu 24.04 LTS
- Linux kernel 6.8 或相近版本
- 已安裝目前核心版本對應的 headers
- 具備 `sudo` 權限

必要套件：

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r) kmod gcc make bc
```

補充：`bc` 是 `scripts/03_benchmark.sh` 用來計算資料量的工具。若只直接執行 `user/benchmark`，不一定需要 `bc`。

---

## 5. 建置、載入與執行

請在專案根目錄執行：

```bash
sudo bash scripts/01_setup.sh
```

此腳本會做四件事：

1. 檢查 root 權限
2. 安裝或確認建置套件
3. 編譯 kernel modules 與 user programs
4. 載入 `mq_module.ko`、`shm_module.ko`，並建立 `/dev/mq_ipc`、`/dev/shm_ipc`

確認裝置是否存在：

```bash
ls -lh /dev/mq_ipc /dev/shm_ipc
cat /proc/mq_stats
cat /proc/shm_stats
```

---

## 6. 跑 demo

demo 的訊息數很少，適合先理解資料怎麼流動：

```bash
sudo bash scripts/02_demo.sh
```

你會看到兩段：

- `mq_demo`：使用 `write()` 將訊息送進 `kfifo`，再用 `read()` 取出。
- `shm_demo`：使用 `mmap()` 取得 shared region 指標，直接寫入 `data[head]`、讀取 `data[tail]`。

若想分開跑：

```bash
cd user
./mq_demo
./shm_demo
```

---

## 7. 跑 benchmark

使用腳本：

```bash
sudo bash scripts/03_benchmark.sh
```

指定訊息數：

```bash
sudo bash scripts/03_benchmark.sh 500000
```

或直接執行 binary：

```bash
cd user
./benchmark
./benchmark 500000
```

輸出中常見欄位：

| 欄位 | 說明 |
| --- | --- |
| `producer` | 生產者執行緒送出指定筆數訊息所花時間。 |
| `consumer` | 消費者執行緒收完指定筆數訊息所花時間。 |
| `wall` | 從建立測試到兩個執行緒都結束的總時間。 |
| `msg/s` | 每秒可處理的訊息數，越高代表吞吐量越高。 |
| `/proc/*_stats` | kernel module 內部統計，適合看計數與平均延遲。 |

`user/benchmark.c` 為了讓測量流程簡單，使用兩個 pthread 模擬 producer / consumer。IPC 入口仍然是 `/dev/mq_ipc` 與 `/dev/shm_ipc`，所以重點在比較資料路徑成本，而不是測量 `fork()` 或 process 啟動成本。

即時觀察：

```bash
watch -n 1 cat /proc/mq_stats
watch -n 1 cat /proc/shm_stats
```

---

## 8. 清理環境

測試完成後卸載 module：

```bash
sudo bash scripts/04_cleanup.sh
```

確認已移除：

```bash
lsmod | grep -E "mq_module|shm_module"
ls /dev/mq_ipc /dev/shm_ipc
ls /proc/mq_stats /proc/shm_stats
```

若上述指令找不到檔案或 module，代表清理完成。

---

## 9. 常見問題與開發 BUG 摘要

詳細分析寫在 [report_ipc.md](report_ipc.md) 與 [report_ipc_api.md](report_ipc_api.md)。這裡先列出最容易踩到的點。

| 問題 | 現象 | 原因 | 處理方向 |
| --- | --- | --- | --- |
| kernel API 版本差異 | module 編譯失敗，例如 `class_create` 參數數量不符 | Linux 6.4 後 `class_create()` 介面改成單參數 | 使用 `class_create("name")`，不要再傳 `THIS_MODULE` |
| `vm_flags` 寫法變更 | `vma->vm_flags` 直接寫入可能在新核心失敗 | 新版核心改用 helper 管理 VMA flags | 使用 `vm_flags_set(vma, flags)` |
| false sharing | mmap 路徑吞吐量不穩或偏低 | `head` 與 `tail` 若落在同一條 cache line，兩個 CPU 會互相使快取失效 | 把 `head`、`tail` 分到不同 64-byte cache line |
| user/kernel layout 風險 | mmap path 與 syscall path 混用時可能讀到不同 slot 位置 | `kernel/shm_module.c` 與 `user/common.h` 手動維護 shared struct layout | 加入 layout 檢查，讓兩邊 `data` offset 完全一致 |
| `bc` 未安裝 | `03_benchmark.sh` 計算資料量時失敗 | 腳本使用 `bc`，但環境可能沒有 | 安裝 `bc` 或改用 shell/awk 計算 |
| `copy_from_user()` 放在 `spinlock` 區段 | 特定情況可能出現核心警告或延遲問題 | user pointer copy 可能觸發 page fault，不適合放在不可睡眠鎖內 | 先 copy 到暫存 buffer，再進入 spinlock 更新 ring |

---

## 10. 建議閱讀順序

1. 先讀本 README，確認專案怎麼跑。
2. 再讀 [report_ipc.md](report_ipc.md)，理解設計、效能對照與除錯紀錄。
3. 最後讀 [report_ipc_api.md](report_ipc_api.md)，對照每個 API、callback、資料流與選擇依據。
