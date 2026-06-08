# Linux IPC Benchmark: Message Queue vs Shared Memory

[![Kernel Version](https://img.shields.io/badge/Kernel-6.8%2B-orange.svg)](https://kernel.org)
[![Platform](https://img.shields.io/badge/Platform-Ubuntu%2024.04-blue.svg)](https://ubuntu.com)
[![License](https://img.shields.io/badge/License-GPL--2.0-green.svg)](../LICENSE)

這個專案用兩個 Linux Kernel Module 實作行程間通訊（Inter-Process Communication, IPC），並用同一組 benchmark 比較 **三種** 資料傳遞路徑：

1. **MQ syscall**：Message Queue，對 `/dev/mq_ipc` 呼叫 `write()` / `read()`。
2. **SHM syscall**：Shared Memory 的 syscall 對照組，對 `/dev/shm_ipc` 呼叫 `write()` / `read()`，內部使用 `spinlock` 和 kernel 端 ring buffer，仍會走 `copy_from_user()` / `copy_to_user()`。
3. **SHM mmap**：Shared Memory 的 mapped page 路徑，先對 `/dev/shm_ipc` 呼叫一次 `mmap()`，之後 producer / consumer 直接操作 user-space 看到的 `shm_region_t`。

所以正式 benchmark 是三種測試，不是四種。`copy_from_user()`、`copy_to_user()`、`spinlock` 是第 2 種 **SHM syscall** 的實作細節，不是另一個獨立測試。

專案透過自製的最小核心模組，觀察「資料複製次數」、「系統呼叫成本」和「同步方式」如何影響 IPC 效能。

---

## 1. 先看結論

每筆訊息固定為 64 bytes。`user/benchmark.c` 依序跑下面三項：

| 編號 | 測試名稱 | 使用裝置與入口 | Kernel 端資料結構 | 每筆訊息是否進核心 | user/kernel copy | 同步與等待方式 | 觀察重點 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | MQ syscall | `/dev/mq_ipc` + `write()` / `read()` | `kfifo` | 是 | 2 次：`copy_from_user()` + `copy_to_user()` | `mutex` + `wait_queue` | 傳統 syscall queue 的成本 |
| 2 | SHM syscall | `/dev/shm_ipc` + `write()` / `read()` | kernel `struct shm_region` ring | 是 | 2 次：`copy_from_user()` + `copy_to_user()` | `spinlock`；目前 `copy_from_user()` 在 lock 內，是已知風險 | 同樣有 copy，但 queue 換成 ring |
| 3 | SHM mmap | `/dev/shm_ipc` + `mmap()`，runtime 直接讀寫 `shm_region_t` | `vmalloc` pages 映射到 user VMA | 只有 setup 時進核心；每筆訊息不走 `read/write` syscall | 0 次 user/kernel copy；仍有 user-space `memcpy()` 到 mapped page | user-space SPSC 輪詢 + `__sync_synchronize()` | 取消每筆 syscall 與 user/kernel copy 的成本差 |

```mermaid
flowchart TB
    subgraph T1["Test 1: MQ syscall"]
        A1["producer thread"] --> B1["write(/dev/mq_ipc)"]
        B1 --> C1["mq_write(): copy_from_user()"]
        C1 --> D1["kernel kfifo"]
        D1 --> E1["mq_read(): copy_to_user()"]
        E1 --> F1["read(/dev/mq_ipc)"]
        F1 --> G1["consumer thread"]
    end

    subgraph T2["Test 2: SHM syscall"]
        A2["producer thread"] --> B2["write(/dev/shm_ipc)"]
        B2 --> C2["shm_write(): spin_lock + copy_from_user()"]
        C2 --> D2["kernel struct shm_region data offset 136"]
        D2 --> E2["shm_read(): memcpy + copy_to_user()"]
        E2 --> F2["read(/dev/shm_ipc)"]
        F2 --> G2["consumer thread"]
    end

    subgraph T3["Test 3: SHM mmap"]
        A3["producer thread"] --> B3["user memcpy to shm_region_t data offset 192"]
        B3 --> C3["mapped vmalloc pages"]
        C3 --> D3["consumer memcpy from shm_region_t data offset 192"]
        D3 --> E3["consumer thread"]
    end
```

這份 benchmark 不保證固定快幾倍，因為結果會受 CPU、核心版本、排程、背景負載影響。它真正要回答的是：成本差異來自 syscall、user/kernel copy、queue/ring 結構，還是同步方式。

注意：本專案裡的 **zero-copy** 是限定用語，只代表第 3 種 SHM mmap 每筆訊息不呼叫 `copy_from_user()` / `copy_to_user()`，不代表完全沒有記憶體複製。程式仍會在 user space 用 `memcpy()` 把資料寫入或讀出 mapped page。

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
| 零複製 | Zero-copy | 本專案限定指「每筆訊息不經 `copy_from_user()` / `copy_to_user()` 跨越 user/kernel 邊界」。它不等於沒有 `memcpy()`，也不等於沒有 cache 或同步成本。 |
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

這些套件的目的：

| 套件 / 關鍵字 | English | 目的 | 為什麼需要 |
| --- | --- | --- | --- |
| `build-essential` | Build Essential Tools | 安裝基本 C/C++ 編譯工具。 | user-space 程式與部分建置流程需要 `gcc`、`make` 等工具。 |
| `linux-headers-$(uname -r)` | Linux Kernel Headers | 提供目前執行中 kernel 的標頭檔與 Kbuild 設定。 | kernel module 必須用「目前這顆 kernel」對應的 headers 編譯，否則容易載入失敗。 |
| `kmod` | Kernel Module Tools | 提供 `insmod`、`rmmod`、`lsmod` 等工具。 | 本專案會載入與卸載 `.ko` kernel module。 |
| `gcc` | GNU C Compiler | 編譯 user-space C 程式。 | `mq_demo`、`shm_demo`、`benchmark` 都是 C 程式。 |
| `make` | Build Automation Tool | 依照 Makefile 執行建置規則。 | 專案用 Makefile 管理 kernel 與 user 兩邊的建置。 |
| `bc` | Basic Calculator | 在 shell script 中做小數運算。 | benchmark script 會用它計算測試資料量 MB。 |

關鍵字補充：

- **Kernel headers**：kernel module 不是一般程式。它會連到 kernel 內部 API，所以編譯時需要和目前 kernel 版本相符的 headers。
- **Kbuild**：Linux kernel 官方的建置系統。`kernel/Makefile` 會把 module build 交給 `/lib/modules/$(uname -r)/build`。
- **`.ko`**：Kernel Object，編譯完成的 kernel module 檔案。此專案會產生 `mq_module.ko` 與 `shm_module.ko`。
- **root 權限**：載入 kernel module 會改變 kernel runtime 狀態，所以需要 `sudo`。

---

## 5. 建置、載入與執行

請在專案根目錄執行：

```bash
sudo bash scripts/01_setup.sh
```

這個步驟的目的，是把「原始碼」變成「可以被 kernel 載入、可以被 user program 操作」的完整測試環境。

整體流程如下：

```text
README 指令
  -> scripts/01_setup.sh
     -> 檢查 root 權限
     -> 安裝建置依賴
     -> 編譯 kernel modules
     -> 編譯 user-space programs
     -> insmod 載入 modules
     -> 建立 /dev/mq_ipc、/dev/shm_ipc
     -> 建立 /proc/mq_stats、/proc/shm_stats
```

此腳本會做四件事：

1. 檢查 root 權限
2. 安裝或確認建置套件
3. 編譯 kernel modules 與 user programs
4. 載入 `mq_module.ko`、`shm_module.ko`，並建立 `/dev/mq_ipc`、`/dev/shm_ipc`

### 5.1 `01_setup.sh` 每一步在做什麼

| 階段 | 腳本動作 | 目的 | 原因 |
| --- | --- | --- | --- |
| 0. root check | 檢查 `$EUID` 是否為 0 | 確認有權限載入 kernel module。 | `insmod`、修改 `/dev` 權限都需要 root。 |
| 1. 顯示環境 | 顯示 `uname -r` 與 OS 版本 | 確認目前 kernel 版本。 | kernel headers 必須和正在跑的 kernel 對應。 |
| 2. 安裝依賴 | `apt-get install build-essential linux-headers... kmod` | 準備編譯與 module 管理工具。 | 少了 headers 會無法編 kernel module；少了 kmod 會無法載入 module。 |
| 3. 編譯 kernel | `make -C "${PROJECT_DIR}" kernel` | 產生 `mq_module.ko`、`shm_module.ko`。 | `.ko` 是 kernel 可載入的 module 格式。 |
| 4. 編譯 user | `make -C "${PROJECT_DIR}" user` | 產生 `mq_demo`、`shm_demo`、`benchmark`。 | demo 與 benchmark 是用來操作 `/dev/*` 的 user-space 程式。 |
| 5. 載入 module | `insmod kernel/*.ko` | 把 IPC 實作加入目前 kernel runtime。 | 未載入前，不會有 `/dev/mq_ipc` 與 `/dev/shm_ipc`。 |
| 6. 設定權限 | `chmod 666 /dev/mq_ipc /dev/shm_ipc` | 讓測試程式可以開啟裝置。 | character device 預設權限可能只允許 root 使用。 |
| 7. 驗證狀態 | `lsmod`、`ls -lh /dev/*`、`cat /proc/*_stats` | 確認 module、device、stats 都存在。 | 建置成功不代表載入成功；這一步確認 runtime 狀態。 |

### 5.2 建置後會產生什麼

| 產物 | 位置 | 用途 |
| --- | --- | --- |
| `mq_module.ko` | `kernel/mq_module.ko` | Message Queue kernel module，提供 `/dev/mq_ipc`。 |
| `shm_module.ko` | `kernel/shm_module.ko` | Shared Memory kernel module，提供 `/dev/shm_ipc` 與 `mmap()`。 |
| `mq_demo` | `user/mq_demo` | 少量訊息佇列 demo，方便逐筆觀察。 |
| `shm_demo` | `user/shm_demo` | shared memory mmap demo，觀察直接讀寫 mapped pages。 |
| `benchmark` | `user/benchmark` | 三種 IPC 路徑的吞吐量測試。 |

### 5.3 載入後會出現什麼 runtime 介面

| 介面 | English | 目的 | 如何使用 |
| --- | --- | --- | --- |
| `/dev/mq_ipc` | Character Device | MQ 的 user/kernel 入口。 | user 程式用 `open()`、`write()`、`read()` 操作。 |
| `/dev/shm_ipc` | Character Device | SHM 的 user/kernel 入口。 | user 程式可用 `read()` / `write()`，也可用 `mmap()`。 |
| `/proc/mq_stats` | procfs Stats File | 觀察 MQ module 內部統計。 | `cat /proc/mq_stats` 或 `watch -n 1 cat /proc/mq_stats`。 |
| `/proc/shm_stats` | procfs Stats File | 觀察 SHM module 內部統計。 | `cat /proc/shm_stats` 或 `watch -n 1 cat /proc/shm_stats`。 |

關鍵字補充：

- **`insmod`**：把 `.ko` 檔載入目前 kernel。載入成功後，`module_init()` 會被呼叫。
- **`lsmod`**：列出目前已載入的 kernel modules。可用來確認 `mq_module`、`shm_module` 是否存在。
- **`/dev`**：device node 所在目錄。user program 開啟 `/dev/mq_ipc` 時，會進入 kernel module 註冊的 `file_operations`。
- **`/proc`**：kernel 暴露狀態資訊的虛擬檔案系統。本專案用它顯示統計，不拿來傳資料。
- **`chmod 666`**：讓所有使用者可讀寫該 device node。這是為了測試方便；正式系統通常會設計更嚴格的權限。

確認裝置是否存在：

```bash
ls -lh /dev/mq_ipc /dev/shm_ipc
cat /proc/mq_stats
cat /proc/shm_stats
```

如果這四個檢查都正常，代表：

1. kernel modules 已載入。
2. character devices 已建立。
3. `/proc` 統計介面已建立。
4. user-space demo 與 benchmark 已經有可操作的 IPC 入口。

---

## 6. 跑 demo

demo 的訊息數很少，適合先理解資料怎麼流動：

```bash
sudo bash scripts/02_demo.sh
```

demo 使用少量訊息呈現兩種 IPC 路徑的工作方式。建議先跑 demo，再跑 benchmark。

`02_demo.sh` 會分成兩段，並在每段執行前停下來等你按 Enter：

- `mq_demo`：使用 `write()` 將訊息送進 `kfifo`，再用 `read()` 取出。
- `shm_demo`：使用 `mmap()` 取得 shared region 指標，直接寫入 `data[head]`、讀取 `data[tail]`。

### 6.1 DEMO 流程總覽

```text
scripts/02_demo.sh
  -> 檢查 /dev/mq_ipc 與 /dev/shm_ipc 是否存在
  -> Part 1: 執行 user/mq_demo
       -> open("/dev/mq_ipc")
       -> write() 8 筆訊息
       -> read()  8 筆訊息
       -> cat /proc/mq_stats
  -> Part 2: 執行 user/shm_demo
       -> open("/dev/shm_ipc")
       -> mmap() mapped pages
       -> 直接寫入 data[head]
       -> 直接讀取 data[tail]
       -> cat /proc/shm_stats
  -> 最後並排顯示兩份 /proc 統計
```

### 6.2 Part 1：`mq_demo` 的目的與資料路徑

`mq_demo` 要示範傳統 syscall 形式的訊息傳遞。它把每筆訊息寫到 `/dev/mq_ipc`，kernel module 會把資料放進 `kfifo`，再由 read path 取回。

```text
mq_demo
  -> write(fd, msg, 64)
     -> mq_write()
        -> copy_from_user()
        -> kfifo_in()

  -> read(fd, buf, 64)
     -> mq_read()
        -> kfifo_out()
        -> copy_to_user()
```

為什麼要看這段：

- 它展示 user program 無法直接碰 kernel FIFO，必須透過 syscall。
- 它展示資料會被複製進 kernel，再複製回 user。
- 它展示 Message Queue 的 FIFO 順序：先寫入的訊息會先讀出。

`mq_demo` 輸出中的關鍵字：

| 關鍵字 | English | 涵義 |
| --- | --- | --- |
| `Producer` | Producer | 生產者，負責送出訊息。 |
| `Consumer` | Consumer | 消費者，負責取出訊息。 |
| `enq` | Enqueue | 將訊息放入佇列。 |
| `deq` | Dequeue | 從佇列取出訊息。 |
| `kfifo` | Kernel FIFO | kernel 內建 FIFO buffer，本專案用它保存 MQ 訊息。 |
| `copy_from_user` | Copy From User | 從 user buffer 安全複製資料到 kernel。 |
| `copy_to_user` | Copy To User | 從 kernel 安全複製資料回 user buffer。 |

可以觀察 `/proc/mq_stats`：

| 欄位 | 意義 | 怎麼判讀 |
| --- | --- | --- |
| `enqueue_count` | 寫入 FIFO 的訊息數 | 跑完 demo 後應該增加。 |
| `dequeue_count` | 從 FIFO 讀出的訊息數 | 若 demo 完整讀完，通常會和 enqueue 接近。 |
| `fifo_used_bytes` | FIFO 目前使用量 | demo 結束且資料都讀完時通常會回到 0。 |
| `fifo_free_bytes` | FIFO 剩餘空間 | 可用來理解 queue 是否接近滿。 |

### 6.3 Part 2：`shm_demo` 的目的與資料路徑

`shm_demo` 要示範 zero-copy 的核心想法：先用 `mmap()` 建立 shared memory mapping，之後每筆訊息直接寫進 user-space 看到的 `shm_region_t`。

```text
shm_demo
  -> mmap("/dev/shm_ipc")
     -> shm_mmap()
        -> vmalloc_to_pfn()
        -> remap_pfn_range()
        -> user 取得 shm_region_t *

  -> producer:
       shm->data[head] = message
       memory barrier
       shm->head.value = next

  -> consumer:
       等待 head != tail
       memory barrier
       讀取 shm->data[tail]
       shm->tail.value = next
```

為什麼要看這段：

- 它展示 `mmap()` 只負責建立映射，不是每筆訊息都呼叫 kernel。
- 它展示 shared memory 的速度來源：少掉每筆 `copy_from_user()` / `copy_to_user()`。
- 它也展示 shared memory 的責任：使用者程式要自己管理 `head`、`tail`、full/empty 判斷與 memory barrier。
- 它目前使用 `volatile` 與 `__sync_synchronize()`，只適合這份 benchmark 的單一 producer / 單一 consumer（SPSC）情境；完整 lock-free queue 需要更嚴謹的 atomic 設計。

`shm_demo` 輸出中的關鍵字：

| 關鍵字 | English | 涵義 |
| --- | --- | --- |
| `mmap OK` | Mapping Success | user program 已取得 mapped region 的虛擬位址。 |
| `userspace ptr` | Userspace Pointer | `mmap()` 回傳的指標，指向 user address space 中的 mapped region。 |
| `slot` | Ring Slot | ring buffer 中的一格，每格存一筆固定 64 bytes 訊息。 |
| `head` | Head Index | 下一個寫入位置。producer 成功寫入後會推進它。 |
| `tail` | Tail Index | 下一個讀取位置。consumer 成功讀取後會推進它。 |
| `memory barrier` | Memory Barrier | 確保資料內容先可見，再更新 `head` 或讀取資料。 |

可以觀察 `/proc/shm_stats`：

| 欄位 | 意義 | 怎麼判讀 |
| --- | --- | --- |
| `ring_capacity` | ring slot 總數 | 本專案預設 512。 |
| `mmap_size_bytes` | mmap 暴露給 user 的大小 | 用來確認 shared region 映射大小。 |
| `ring_used_slots` | ring 目前有多少 slot 被使用 | demo 結束且資料讀完時通常接近 0。 |
| `ring_free_slots` | ring 剩餘可寫 slot | ring 滿時 producer 需要等待。 |
| `write_count` / `read_count` | syscall path 統計 | mmap path 每筆訊息不一定會增加這兩個欄位，因為它不走 `shm_write()` / `shm_read()`。 |

### 6.4 SHM layout 現況：為什麼不能把兩條 SHM path 混著看

`/dev/shm_ipc` 同時提供 syscall path 與 mmap path，但目前 kernel 與 user 對 `data` 欄位的 offset 不一致：

| 位置 | `head` offset | `tail` offset | metadata offset | `data` offset |
| --- | ---: | ---: | ---: | ---: |
| `kernel/shm_module.c` 的 `struct shm_region` | 0 | 64 | `capacity=128`, `msg_size=132` | 136 |
| `user/common.h` 的 `shm_region_t` | 0 | 64 | `meta=128..191` | 192 |

這代表：

- `head` / `tail` offset 相同，所以 `/proc/shm_stats` 仍能觀察 mmap path 推進後的 ring index。
- `data` offset 不同，所以不要把「SHM syscall 寫入、SHM mmap 讀出」或反過來當成目前已支援的功能。
- `shm_demo` 和 benchmark 的第 3 種測試是 producer / consumer 都依照 user `shm_region_t` layout 操作，因此單獨跑 mmap path 不一定會立刻出錯。

若要把 syscall path 和 mmap path 真的做成同一份 ABI，應先讓兩邊共用 layout header，或加入 `offsetof(data)` 檢查。

### 6.5 為什麼 demo 要拆成 MQ 與 SHM mmap

| 比較點 | `mq_demo` | `shm_demo` |
| --- | --- | --- |
| 入口 | `write()` / `read()` | `mmap()` 後直接讀寫指標 |
| 資料是否每筆進 kernel | 是 | 否，只有建立映射時進 kernel |
| user/kernel copy | 有 | 沒有 `copy_from_user()` / `copy_to_user()` |
| 同步責任 | kernel queue 與 wait queue 負責較多 | user-space ring protocol 負責較多 |
| 適合觀察 | FIFO、enqueue/dequeue、syscall copy | head/tail、slot、zero-copy |

先看 demo 的好處是：benchmark 數字出現前，你已經知道每個數字背後代表哪條資料路徑。

若想分開跑：

```bash
cd user
./mq_demo
./shm_demo
```

分開跑時要注意：仍然要先執行 `sudo bash scripts/01_setup.sh`，因為 demo 需要 `/dev/mq_ipc`、`/dev/shm_ipc` 已存在。

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
