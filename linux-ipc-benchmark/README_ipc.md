# Linux IPC Benchmark: Message Queue vs. Shared Memory

[![Kernel Version](https://img.shields.io/badge/Kernel-6.8%2B-orange.svg)](https://kernel.org)
[![Platform](https://img.shields.io/badge/Platform-Ubuntu%2024.04-blue.svg)](https://ubuntu.com)
[![License](https://img.shields.io/badge/License-GPL--2.0-green.svg)](LICENSE)

本專案是一個深度的 Linux 核心實驗，旨在透過實作核心模組（Kernel Module）來量化比較兩大行程間通訊（IPC）機制的效能：**訊息佇列 (Message Queue)** 與 **共享記憶體 (Shared Memory)**。

與其僅僅閱讀教科書上的理論，本專案直接在核心空間實作了 `mq_module.ko` 與 `shm_module.ko`，讓開發者能從指令層級觀察「資料複製次數」與「系統呼叫 (Syscall) 開銷」如何影響系統吞吐量。

---

## 🌟 專案亮點

- **核心級實作**：不使用既有的 POSIX/System V API，而是直接利用 Linux 核心的原生機制（`kfifo`、`vmalloc`、`remap_pfn_range`）從頭建構 IPC 通道。
- **三種路徑對照**：
  1. **MQ (kfifo + Syscall)**：標準生產者-消費者模型，涉及兩次核心/使用者空間資料複製。
  2. **SHM (Syscall Path)**：在共享記憶體架構下仍使用 `read`/`write`，用以隔離「資料拷貝」與「核心排程」的開銷。
  3. **SHM (mmap Zero-Copy)**：真正的零拷貝實作，訊息傳遞完全在使用者空間完成，不經過任何 Syscall。
- **即時遙測 (Observability)**：透過 `/proc/mq_stats` 與 `/proc/shm_stats` 暴露核心內部的平均延遲與佇列狀態，實現精確的效能監控。

---

## 📂 專案架構

```text
linux-ipc-benchmark/
├── kernel/             # Linux 核心模組原始碼
│   ├── mq_module.c     # 基於 kfifo 的訊息佇列實作
│   └── shm_module.c    # 基於 vmalloc + mmap 的共享記憶體實作
├── user/               # 使用者空間工具與效能測試程式
│   ├── benchmark.c     # 三路徑吞吐量對比工具
│   ├── mq_demo.c       # 訊息佇列基本操作示範
│   └── shm_demo.c      # 共享記憶體 mmap 操作示範
├── scripts/            # 自動化維運腳本 (環境建置、執行、清理)
└── docs/               # 專案執行截圖與架構圖
```

---

## 🛠️ 環境建置與權限需求

### 前置要求
- **作業系統**：Ubuntu 24.04 LTS (核心版本建議 6.8+)
- **權限**：載入核心模組與操作 `/dev` 設備需要 **root 權限**。

### 安裝必要套件
```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r) kmod gcc make
```

---

## 🚀 部署與執行流程

為了獲得最完整的觀測經驗，建議您開啟 **三個終端機視窗 (Terminals)**。

### 步驟 1：環境初始化 (Terminal 1)
此腳本會自動編譯核心模組、載入設備，並修正 `/dev/` 節點權限。
```bash
# 確保位於專案根目錄
sudo bash scripts/01_setup.sh
```

### 步驟 2：即時狀態監控 (Terminal 2)
在新的視窗中，監控核心內部的統計數據變化：
```bash
# 監控訊息佇列狀態
watch -n 1 cat /proc/mq_stats
# (或者是監控共享記憶體)
# watch -n 1 cat /proc/shm_stats
```

### 步驟 3：執行效能測試 (Terminal 3)
在主視窗執行基準測試，對比三種模式的吞吐量：
```bash
# 預設測試 200,000 筆訊息，每筆 64 bytes
cd user
./benchmark
```

---

## 🎬 功能驗證展示

### 展示 1：訊息佇列阻塞特性
您可以使用 `mq_demo` 觀察當佇列滿載時，生產者如何自動進入睡眠狀態：
```bash
# 在 Terminal 3 執行生產者
./mq_demo producer
# 在 Terminal 2 觀察 /proc/mq_stats 的 fifo_used_bytes 變化
```

### 展示 2：共享記憶體零拷貝
執行 `benchmark` 時，觀察 Test [3] 的數據。您會發現其吞吐量通常是 Test [1] 的 **5 到 8 倍**，這正是消除了 `copy_from_user` 與 `copy_to_user` 後的真實效能釋放。

---

## 🧹 清理與還原
測試完成後，請務必卸載模組：
```bash
sudo bash scripts/04_cleanup.sh
```

---

## 🔍 未來延伸探討

1. **快取偽共享 (False Sharing) 的影響**：目前 `shm_region` 已加入 60 位元組的填充（padding），可進一步研究不同 CPU 架構下快取行對齊對效能的提升。
2. **Lock-free 佇列實作**：目前的 `mmap` 路徑使用簡單的記憶體屏障，可嘗試導入更複雜的 CAS (Compare-and-Swap) 指令來實現多生產者/多消費者的無鎖化。
3. **Huge Pages 整合**：透過映射大頁（Large Pages）來減少 TLB Miss，觀察對極大規模共享記憶體存取的效能影響。
4. **核心同步原語對比**：比較 `mutex`、`spinlock` 與 `wait_queue` 在極高頻率 IPC 呼叫下的負載特性。

---

詳細的實作細節與核心程式碼解析，請參閱：👉 [**技術報告 (report_ipc.md)**](report_ipc.md)
