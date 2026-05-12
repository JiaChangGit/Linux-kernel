# Linux IPC Benchmark 技術報告

## 1. 報告定位與結論摘要

本專案不是直接呼叫 Linux 既有的 **POSIX Message Queue**、**System V Message Queue**、**POSIX Shared Memory** 或 **System V Shared Memory** API，而是自己撰寫兩個 Linux 核心模組（kernel modules）來模擬與對照兩種 IPC（Inter-Process Communication，行程間通訊）設計：

1. `kernel/mq_module.c`
   以 `kfifo` 為核心，透過字元裝置（character device）`/dev/mq_ipc` 提供固定長度訊息佇列。
2. `kernel/shm_module.c`
   以 `vmalloc()` 配置共享環形緩衝區（ring buffer），透過 `/dev/shm_ipc` 提供兩條路徑：
   - `write()` / `read()` 的 syscall 路徑
   - `mmap()` 後的 zero-copy 路徑

這個專案的核心教學目標，不是只陳述「Shared Memory 比 Message Queue 快」，而是把原因拆成三個可檢查、可對照、可量測的面向：

1. **資料複製次數（copy count）**
2. **每筆訊息是否需要 syscall**
3. **同步機制（synchronization）的成本**

若只看目前專案的程式實作，最核心的結論是：

- `mq_module` 的訊息傳遞，每筆資料要經過 `copy_from_user()` 與 `copy_to_user()`，也就是兩次 user/kernel 邊界資料複製。
- `shm_module` 的 syscall 路徑，雖然底層結構換成 ring buffer，但仍然有兩次複製，因此它用來隔離「佇列機制」本身的差異。
- `shm_module` 的 `mmap()` 路徑把同一批實體頁面（physical pages）同時映射到使用者空間與核心空間，訊息傳遞時不再需要 `copy_from_user()` / `copy_to_user()`，也不需要每筆訊息都進入核心。在 `README_ipc.md` 的範例 benchmark 中，這一路徑的 wall throughput 是 `2,692,462 msg/s`，相對 MQ 的 `478,399 msg/s` 為 `5.63×`。

本專案另外實作了一層輕量級的 **trace / tracing / observability（追蹤 / 觀測）** 機制，但要精確描述：它不是 Linux 正規的 **tracepoint**、`ftrace`、`perf`、`eBPF` tracing，而是：

- 核心內以 `ktime_get()` 記錄時間
- 以 `atomic64_t` 累計統計
- 透過 `/proc/mq_stats` 與 `/proc/shm_stats` 導出結果
- 在 userspace 以 `clock_gettime(CLOCK_MONOTONIC)` 額外量測 demo 與 benchmark 耗時

所以這個專案的 trace，本質上是 **自製的事件觀測與統計匯出機制**，不是通用型 kernel tracing framework。

---

## 2. 專案目標與研究問題

本專案要回答的不是抽象問題，而是以下三個具體技術問題：

### 2.1 Message Queue 為什麼比 `mmap` 路徑慢？

因為訊息必須先從 userspace 複製到 kernel buffer，再從 kernel buffer 複製回另一側 userspace buffer。這兩次 copy 會帶來：

- 額外 CPU 指令成本
- 快取污染（cache pollution）
- syscall 進出核心成本
- queue 滿/空時的等待與喚醒成本

### 2.2 Shared Memory 為什麼快？

因為 `mmap()` 建立後，資料已經位於雙方共享的頁面中。在本專案的 `mmap` 路徑裡，傳遞訊息時只會修改共享記憶體中的 `data[slot]`、`head`、`tail`，不會對每筆訊息額外呼叫 `write()` / `read()` 進入 kernel。

### 2.3 Trace 在這裡扮演什麼角色？

Trace 的功能在這個專案中有兩個層次：

1. **效能觀測（performance observability）**
   量測 write/read、producer/consumer、整體 wall-clock throughput。
2. **機制驗證（mechanism verification）**
   驗證 queue 是否有進出、共享記憶體是否有被消費、平均延遲是否有累積。

因此，trace 在這裡不是為了完整事件重建（event reconstruction），而是為了支撐「資料到底走哪條路、花了多少時間、目前佇列狀態如何」這三件事。

---

## 3. 專案架構總覽

### 3.1 元件分層

專案由三層構成：

1. **Kernel space（核心空間）**
   - `kernel/mq_module.c`
   - `kernel/shm_module.c`
2. **User space（使用者空間）**
   - `user/mq_demo.c`
   - `user/shm_demo.c`
   - `user/benchmark.c`
   - `user/common.h`
3. **Automation scripts（自動化腳本）**
   - `scripts/01_setup.sh`
   - `scripts/02_demo.sh`
   - `scripts/03_benchmark.sh`
   - `scripts/04_cleanup.sh`

### 3.2 字元裝置（Character Device）

兩個核心模組都不是建立新的 syscall，而是註冊成字元裝置：

- `/dev/mq_ipc`
- `/dev/shm_ipc`

這代表 userspace 可以用標準檔案介面操作：

- `open()`
- `read()`
- `write()`
- `mmap()`（僅 `shm_module`）

這種做法的教學優點是：

- 不需修改系統呼叫表
- 介面統一
- 可直接用 shell、C 程式與 benchmark 工具操作

### 3.3 `/proc` 統計介面

兩個模組都透過 `/proc` 暴露統計資訊：

- `/proc/mq_stats`
- `/proc/shm_stats`

這些檔案不是資料傳輸通道，而是 **runtime statistics endpoint（執行期統計匯出端點）**。

---

## 4. 重要術語與中英文對照

| 中文 | English | 本專案中的具體意義 |
|---|---|---|
| 行程間通訊 | Inter-Process Communication, IPC | 不同行程或執行緒之間交換資料的方式 |
| 字元裝置 | Character Device | 以 `/dev/*` 形式提供 `read/write/mmap` 介面 |
| 核心模組 | Kernel Module | 可動態載入的 Linux 核心程式，副檔名 `.ko` |
| 環形緩衝區 | Ring Buffer / Circular Buffer | 以 head/tail 表示資料進出位置的固定容量緩衝結構 |
| 先進先出佇列 | FIFO, First-In First-Out | 最先寫入的資料最先被讀出 |
| 共享記憶體 | Shared Memory | 多方看到同一塊實體記憶體頁面 |
| 零拷貝 | Zero-Copy | 傳遞時不需要 `copy_from_user` / `copy_to_user` |
| 記憶體映射 | Memory Mapping, `mmap` | 把某段核心或檔案頁面映射到使用者虛擬位址空間 |
| 互斥鎖 | Mutex | 可能睡眠的鎖，常用於 process context 互斥 |
| 自旋鎖 | Spinlock | 忙等（busy wait）型鎖，不可在持鎖期間睡眠 |
| 等待佇列 | Wait Queue | 讓執行緒在條件不成立時睡眠，條件成立時喚醒 |
| 記憶體屏障 | Memory Barrier | 保證讀寫順序，不讓 CPU 或編譯器任意重排 |
| 可觀測性 | Observability | 系統可被量測、理解、診斷的能力 |
| 追蹤 | Tracing / Trace | 對事件與時間關係進行記錄或導出 |
| 延遲 | Latency | 一筆資料從生產到可被消費所需時間 |
| 吞吐量 | Throughput | 單位時間內完成的訊息數 |

---

## 5. 編譯、載入與執行流程

### 5.1 Makefile 結構

專案最上層 `Makefile` 只做協調：

- `make kernel`
- `make user`
- `make clean`

`kernel/Makefile` 使用 Linux kernel build system：

```make
obj-m := mq_module.o shm_module.o
KDIR := /lib/modules/$(shell uname -r)/build
```

這代表它不是自行手寫完整編譯規則，而是借用 kernel 原生模組建構機制來產出 `.ko`。

`user/Makefile` 使用 `gcc` 編譯三個 userspace 程式，`benchmark` 另外連結 `-lpthread`。

### 5.2 setup script 的角色

`scripts/01_setup.sh` 負責：

1. 檢查是否為 root
2. 安裝 headers 與編譯工具
3. 編譯模組與 userspace 程式
4. `insmod` 載入模組
5. 對 `/dev/mq_ipc`、`/dev/shm_ipc` 設定權限

這個腳本把原本零散的系統準備流程自動化，降低實驗門檻。

---

## 6. Message Queue 路徑實作深入分析

### 6.1 這個 Message Queue 不是 POSIX mq

必須先講清楚：`mq_module.c` 並不是 Linux 現成的 `mq_*` 系列 API，也不是 kernel 內建 message queue。它是：

- 自訂字元裝置
- 自訂 `file_operations`
- 內部以 `kfifo` 儲存訊息

所以這是一個 **teaching-oriented custom message queue implementation（教學導向的自製訊息佇列實作）**。

### 6.2 資料結構

核心資料結構是：

- `DEFINE_KFIFO(g_fifo, char, FIFO_SIZE)`
- `DEFINE_MUTEX(g_lock)`
- `DECLARE_WAIT_QUEUE_HEAD(g_rd_wq)`
- `DECLARE_WAIT_QUEUE_HEAD(g_wr_wq)`

意義如下：

- `g_fifo`
  真正存放訊息內容的 FIFO 緩衝區。
- `g_lock`
  保護 `kfifo_in()` 與 `kfifo_out()` 的互斥存取。
- `g_rd_wq`
  當 queue 為空時，reader 睡眠等待。
- `g_wr_wq`
  當 queue 為滿時，writer 睡眠等待。

### 6.3 固定長度訊息設計

`MSG_SIZE` 被固定為 64 bytes，`QUEUE_DEPTH` 為 512，所以總容量：

```text
FIFO_SIZE = 64 * 512 = 32768 bytes
```

這表示此設計不是 variable-length message queue，而是 **fixed-size message queue（固定大小訊息佇列）**。

固定長度的優點：

- 實作簡單
- 佇列容量明確
- benchmark 可控

限制也很明顯：

- 只能自然處理 64-byte payload
- 小於 64 bytes 的寫入在語意上會被視為 64-byte message
- 大於 64 bytes 的寫入被截斷

### 6.4 `mq_write()` 的路徑

`mq_write()` 的核心步驟如下：

1. 使用 `copy_from_user(kb, ubuf, len)` 把資料從 userspace 複製到 kernel stack buffer `kb`
2. `wait_event_interruptible()` 等待 FIFO 有至少一個訊息槽位
3. `mutex_lock(&g_lock)`
4. `ktime_get()` 取時間戳
5. `kfifo_in(&g_fifo, kb, MSG_SIZE)` 寫入 FIFO
6. `mutex_unlock(&g_lock)`
7. `atomic64_inc(&st_enq)`
8. `WRITE_ONCE(st_last_enq_ts, ts)`
9. `wake_up_interruptible(&g_rd_wq)`

這條路徑的關鍵教學點有三個：

#### 第一，`copy_from_user()` 為什麼必要？

kernel 不能直接相信使用者傳進來的指標可以安全解參考（dereference）。userspace 位址可能：

- 不存在
- 權限不足
- 指向尚未映射的頁面

因此核心必須透過 `copy_from_user()` 做受控存取。

#### 第二，為什麼要先等待再進入 critical section？

因為 queue 滿時不希望忙等，而是希望 producer 睡眠，讓 CPU 讓給其他工作。這就是 **blocking backpressure（阻塞式回壓）**。

#### 第三，時間戳記記錄了什麼？

`st_last_enq_ts` 記錄的是「最近一次 enqueue 時間」，不是每一筆訊息各自獨立的時間戳陣列。因此它比較像 **latest enqueue marker（最近一次入隊標記）**，不是完整事件追蹤表。

### 6.5 `mq_read()` 的路徑

`mq_read()` 的核心步驟：

1. 如果是 `O_NONBLOCK` 且 queue 空，回傳 `-EAGAIN`
2. 否則 `wait_event_interruptible()` 等待至少一筆訊息
3. `mutex_lock(&g_lock)`
4. `kfifo_out(&g_fifo, kb, MSG_SIZE)`
5. `ktime_get()` 取 dequeue 時間
6. `mutex_unlock(&g_lock)`
7. `copy_to_user(ubuf, kb, MSG_SIZE)`
8. 用 `ts - st_last_enq_ts` 累加延遲
9. `atomic64_inc(&st_deq)`
10. 喚醒 `g_wr_wq`

### 6.6 MQ 的同步模型

MQ 使用的是：

- `mutex`
- `wait queue`

這種模型的特性是：

- queue 滿/空時，執行緒可以睡眠，不會一直耗 CPU
- 當 queue full 或 empty 時，執行緒會睡眠並等待條件成立
- 每次 sleep/wakeup 都會引入 scheduler overhead（排程開銷）

### 6.7 MQ trace 與統計輸出

`mq_stats_show()` 透過 `seq_printf()` 輸出：

- `enqueue_count`
- `dequeue_count`
- `avg_latency_ns`
- `fifo_used_bytes`
- `fifo_free_bytes`

這些欄位是透過 `/proc/mq_stats` 導出的。從 tracing 角度來看，這不是 event-by-event log，而是 **aggregated telemetry（聚合遙測統計）**。

---

## 7. Shared Memory 模組實作深入分析

### 7.1 共享記憶體在本專案中的定義

這裡的 shared memory，不是 `shm_open()` 或 `shmget()`，而是：

1. 核心模組用 `vmalloc()` 配出一塊記憶體
2. 該記憶體內部被定義成 ring buffer 結構
3. 透過字元裝置的 `mmap()` file operation，把這塊記憶體映射到 userspace

因此本專案展示的是 **custom kernel-backed shared memory region（核心模組支撐的自製共享記憶體區）**。

### 7.2 `struct shm_region` 的布局

核心定義如下概念：

- `head`: 下一個寫入位置
- `tail`: 下一個讀取位置
- `capacity`: 環形佇列容量
- `msg_size`: 每筆訊息大小
- `data[RING_CAPACITY][MSG_SIZE]`: 真正資料區

這個布局在 `user/common.h` 中以 `shm_region_t` 做鏡射（mirror），兩者必須完全一致。

這種設計是必要條件。因為 `mmap()` 之後，userspace 並不是拿到某種序列化資料，而是直接拿到「核心與使用者共同理解的記憶體結構」。

### 7.3 Cache Line Padding（快取列填充）

`head`、`tail`、`capacity`、`msg_size` 後面接了一段 `_pad[48]`。註解說明它的目的，是讓關鍵欄位盡量分散在 cache line 友善的布局上，減少 **false sharing（偽共享）**。

#### 什麼是 false sharing？

兩個 CPU core 若分別修改不同變數，但這些變數剛好位於同一個 cache line，快取一致性協定仍然會讓它們互相干擾，導致額外 invalidation 與同步成本。

在 ring buffer 中：

- producer 主要更新 `head`
- consumer 主要更新 `tail`

因此程式把控制欄位與資料區分開，減少 producer 與 consumer 對同一 cache line 的競爭。

### 7.4 為什麼使用 `vmalloc()`？

`vmalloc()` 與 `kmalloc()` 的差異，直接決定這個共享區如何被配置與映射。

#### `kmalloc()`

- 要求實體記憶體盡量連續
- 適合小型物件配置
- 配置成功與否取決於可用的連續實體記憶體

#### `vmalloc()`

- 提供虛擬位址上的連續區域
- 底層實體頁面不必連續
- 適合建立以頁為單位管理的虛擬連續區域

本專案的共享區大小是 `PAGE_ALIGN(sizeof(struct shm_region))`。此處採用 `vmalloc()` 有兩個直接效果：

1. 展示 shared memory 不必依賴 physically contiguous memory
2. 展示 `mmap()` 時如何逐頁做 PFN 映射

### 7.5 SHM 的 syscall 路徑

`shm_module.c` 不只提供 `mmap`，也提供 `write()` / `read()`。這讓實驗能做三組對照：

1. MQ: `kfifo + wait queue + 2 copies`
2. SHM syscall: `ring + spinlock + 2 copies`
3. SHM mmap: `ring + direct shared pages + 0 kernel boundary copies`

如此一來就能把「資料結構差異」與「copy/syscall 差異」部分拆開。

#### `shm_write()`

路徑如下：

1. `spin_lock(&g_spin)`
2. 讀取 `head`
3. 計算 `next = (head + 1) % RING_CAPACITY`
4. 若 `next == tail`，表示 ring full，回傳 `-ENOSPC`
5. `copy_from_user(g_shm->data[head], ubuf, len)`
6. `ktime_get()` 取時間戳
7. `smp_wmb()`
8. `g_shm->head = next`
9. `spin_unlock(&g_spin)`
10. 累計 `st_wr`

#### `shm_read()`

路徑如下：

1. `spin_lock(&g_spin)`
2. 若 `head == tail`，表示 ring empty，回傳 `-EAGAIN`
3. 取出 `tail`
4. `ktime_get()`
5. `smp_rmb()`
6. `memcpy(tmp, g_shm->data[tail], MSG_SIZE)`
7. `g_shm->tail = (tail + 1) % RING_CAPACITY`
8. `spin_unlock(&g_spin)`
9. `copy_to_user(ubuf, tmp, MSG_SIZE)`
10. 累計延遲與讀取數

### 7.6 SHM syscall 路徑為什麼在範例 benchmark 中高於 MQ，但低於 `mmap` 路徑？

因為它：

- 仍然需要 `copy_from_user()`
- 仍然需要 `copy_to_user()`
- 仍然每筆訊息都要進入 kernel

在 `README_ipc.md` 的範例 benchmark 中，SHM syscall 路徑的 wall throughput 是 `655,341 msg/s`，高於 MQ 的 `478,399 msg/s`。在目前這份程式碼裡，兩者都保留兩次 copy；差異來自 queue 實作與同步策略不同，前者是 `ring + spinlock`，後者是 `kfifo + mutex + wait queue`。

### 7.7 `mmap()` zero-copy 路徑是整個專案的核心

`shm_mmap()` 是需要拆解的核心函式。重點不是口號式地說「共享記憶體比較快」，而是釐清它如何把 kernel 配置的頁面映射到 userspace。

#### 步驟 1：確認映射大小

```c
unsigned long vm_sz = vma->vm_end - vma->vm_start;
if (vm_sz > SHM_BUF_SIZE)
    return -EINVAL;
```

這表示使用者不能映射超過核心準備好的共享區大小。

#### 步驟 2：設定 VMA flags

```c
vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
```

意義：

- `VM_DONTEXPAND`: 不允許這段映射被隨意擴張
- `VM_DONTDUMP`: 避免核心 dump 時把這段區域無意義地納入

#### 步驟 3：逐頁映射

```c
while (done < vm_sz) {
    unsigned long pfn = vmalloc_to_pfn((void *)kaddr);
    ret = remap_pfn_range(vma, uaddr, pfn, PAGE_SIZE, vma->vm_page_prot);
    ...
}
```

這段是 shared memory 實作的核心步驟。

##### 為什麼不能一次直接映射整塊？

因為 `vmalloc()` 只保證虛擬位址連續，不保證底層實體頁面連續。`remap_pfn_range()` 需要的是每一頁對應的 PFN（Page Frame Number，頁框編號），所以必須：

1. 取出目前 kernel virtual address `kaddr`
2. 用 `vmalloc_to_pfn()` 找到該頁對應的 PFN
3. 把這個 PFN 映射到 userspace VMA 的對應頁
4. 前進到下一頁

這就是 **page-by-page remapping（逐頁重映射）**。

#### 步驟 4：mmap 完成後發生了什麼？

完成後，userspace 的指標 `shm` 與 kernel 的 `g_shm` 雖然不是同一個虛擬位址，但它們對應到的是同一批實體頁面。

也就是說：

- producer 在 userspace 寫 `shm->data[head]`
- consumer 在另一個 userspace context 讀 `shm->data[tail]`

本質上是直接讀寫共享頁面，不需要 kernel 當中介搬運工。

### 7.8 Single Producer Single Consumer（SPSC）

`mmap` 路徑使用的是 **SPSC lock-free ring buffer（單生產者、單消費者無鎖環形佇列）** 思想。

其設計基礎是：

- producer 只寫 `head`
- consumer 只寫 `tail`
- 雙方都可以讀對方的 index

這讓 `mmap` 路徑不需要 `mutex` 或 `spinlock` 來保護 `head` 與 `tail` 的更新。原因是目前模型只允許一個 producer 更新 `head`，一個 consumer 更新 `tail`。

### 7.9 Full / Empty 判斷

本專案沿用 ring buffer 最經典的判斷方式：

- `empty`: `head == tail`
- `full`: `(head + 1) % capacity == tail`

這個設計的代價是，容量 512 不代表可同時放滿 512 筆有效訊息；可安全使用的資料槽位是 511，因為必須保留一個狀態用來區分 full 與 empty。

#### 範例

假設 `capacity = 8`：

- `head = 3`, `tail = 3` 表示 empty
- `head = 7`, `tail = 0` 時，若再寫一次 `next = 0`，就會與 `tail` 相等，因此視為 full

---

## 8. Memory Barrier 與一致性保證

### 8.1 為什麼 `volatile` 不夠？

`head`、`tail` 在結構中被標成 `volatile`，但這不代表就自動擁有正確的跨核心同步語意。

`volatile` 主要只表示：

- 不要把該變數的存取任意優化消除

它**不是**完整的 CPU 記憶體順序保證工具。真正保證順序的是：

- `__sync_synchronize()`
- `smp_wmb()`
- `smp_rmb()`

### 8.2 Producer 為何要先寫資料，再更新 `head`？

假設 producer 反過來做：

1. 先更新 `head`
2. 再寫入 `data[head]`

consumer 就可能看到「head 已前進」，於是去讀一個其實尚未寫完的 slot。這會造成讀到舊值、半寫入值，或未定義狀態。

因此順序必須是：

1. 寫資料
2. memory barrier
3. 更新 `head`

### 8.3 Consumer 為何要先確認狀態，再讀資料？

consumer 需要確保它觀察到的 `head` 與 `data[tail]` 是一致的。否則 CPU/編譯器重排可能讓 consumer 讀到錯誤順序的資料。

### 8.4 本專案中的 barrier 實作

在 kernel syscall 路徑：

- `smp_wmb()`：保證 slot data 寫入先於 `head` 更新
- `smp_rmb()`：保證 consumer 觀察順序

在 userspace mmap 路徑：

- `__sync_synchronize()`：作為 full barrier

### 8.5 舉例說明

以 `user/shm_demo.c` 為例，producer 寫入：

1. `snprintf(shm->data[head], ...)`
2. `__sync_synchronize()`
3. `shm->head = next`

這表示「內容先準備好，再對外宣告這個 slot 可以讀」。

若缺少第 2 步，某些 CPU 架構上就可能讓 consumer 提早看到 `head` 已更新，但內容尚未全數可見。

---

## 9. User Space 程式設計與 benchmark 方法

### 9.1 `user/common.h`

這個檔案不是單純常數宣告，而是 userspace 與 kernel 之間的 **ABI-like shared contract（類 ABI 共用契約）**。

它規定：

- `MSG_SIZE = 64`
- `RING_CAPACITY = 512`
- `shm_region_t` 的記憶體布局
- `SHM_MAP_SIZE` 的 page alignment 方式

只要 `user/common.h` 與 `kernel/shm_module.c` 的結構定義不同步，就可能導致：

- userspace 讀錯欄位
- slot 偏移錯誤
- `mmap` 區域解讀錯誤

所以這種鏡射結構在 shared memory 設計中是高度關鍵的。

### 9.2 `mq_demo.c`

這個程式的功能是把 MQ 的每一步視覺化：

- 連續寫入 8 筆訊息
- 每一筆 write 前後用 `now_us()` 量測
- 再逐筆讀出並量測
- 最後印出 `/proc/mq_stats`

這是一種 **pedagogical trace（教學型追蹤）**：不是高精度 profiling，而是讓學習者看清楚資料流與時間差。

### 9.3 `shm_demo.c`

這個程式展示的是 `mmap` 路徑真正的精神：

1. `open("/dev/shm_ipc", O_RDWR)`
2. `mmap(..., MAP_SHARED, fd, 0)`
3. 直接透過 `shm->data[head]` 寫入資料
4. 直接透過 `shm->data[tail]` 讀出資料

這裡的 `memcpy(snapshot, shm->data[tail], MSG_SIZE)` 並不表示退化成傳統 copy-based IPC，它只是 userspace 為了安全顯示內容、取得局部副本做輸出；重點是**沒有經過 kernel boundary copy**。

### 9.4 `benchmark.c` 的測試設計

`benchmark.c` 一共執行三組測試：

1. MQ syscall path
2. SHM syscall path
3. SHM mmap path

每組測試都有：

- 一個 producer thread
- 一個 consumer thread
- 一個 `pthread_barrier_t`

### 9.5 為什麼要用 `pthread_barrier_t`？

如果不做同步起跑，producer 與 consumer 可能在不同時間開始，會使 benchmark 受 thread scheduling 噪音影響更大。

`pthread_barrier_wait()` 的作用是讓兩個 thread 都到達起跑點後，再一起開始計時區段。

### 9.6 Wall-clock time 的定義

`run_test()` 在建立 thread 前記錄 `wall_start`，兩個 thread `join` 後記錄 `wall_end`。

因此 wall time 包含：

- thread create 開銷
- 真正傳輸時間
- thread join 開銷

這不是純粹的「單一 IPC primitive 本體延遲」，而是比較接近 **end-to-end benchmark time（端到端基準時間）**。三組方法都用同一規則，所以它仍然具有比較價值。

---

## 10. Trace / 追蹤功能實作詳解

這一節專門處理 trace。因為本專案的 trace 功能不是傳統 tracing framework，所以必須精確分析。

## 10.1 先下定義：本專案的 trace 是什麼？

本專案中的 trace，比較準確的說法是：

- **lightweight in-module tracing（模組內輕量追蹤）**
- **latency instrumentation（延遲插樁）**
- **statistics export（統計匯出）**

它沒有做這些事：

- 沒有註冊 kernel tracepoint
- 沒有接 `ftrace`
- 沒有用 `perf_event`
- 沒有用 `eBPF`
- 沒有保留每筆訊息完整事件序列

它有做的事則是：

1. 在資料路徑上插入時間戳（timestamp）
2. 累計計數器（counters）
3. 透過 `/proc` 讓 userspace 可讀
4. 在 demo / benchmark 中讀取並顯示

### 10.2 MQ 的 trace 實作步驟

#### 10.2.1 事件插樁點（instrumentation points）

在 `mq_write()`：

- `ktime_get()` 在資料成功準備入 FIFO 後記錄 enqueue 時間
- `WRITE_ONCE(st_last_enq_ts, ts)` 更新最近一次 enqueue timestamp
- `atomic64_inc(&st_enq)` 記錄 enqueue 計數

在 `mq_read()`：

- `ktime_get()` 在 `kfifo_out()` 後取 read 時間
- `ktime_sub(ts, st_last_enq_ts)` 估算延遲
- `atomic64_add(lat, &st_lat_ns_total)` 累加總延遲
- `atomic64_inc(&st_deq)` 記錄 dequeue 數量

#### 10.2.2 `/proc` 匯出

透過：

- `proc_create("mq_stats", 0444, NULL, &g_proc_ops)`
- `single_open()`
- `seq_read()`
- `seq_printf()`

把統計匯出給 userspace。

這種設計的優點：

- 實作簡單
- 幾乎所有 Linux 系統都能直接 `cat /proc/mq_stats`
- 不需要額外 tracing 工具

#### 10.2.3 這種 trace 的限制

這個設計不是 per-message exact trace，原因是：

- `st_last_enq_ts` 只有一個全域時間戳
- 它不對每個 slot 維護獨立 enqueue timestamp

因此在併發情境下，`avg_latency_ns` 代表的是**以最後一次 enqueue 時間戳估算出的聚合平均值**，不是把每一筆訊息各自配對後算出的平均值。

這個差異會直接影響讀者如何解讀 `avg_latency_ns`，因此不能模糊帶過。

#### 10.2.4 具體例子

假設有兩筆訊息 A、B：

1. A enqueue at `t=10`
2. B enqueue at `t=12`
3. consumer 在 `t=20` 讀到 A

若 `st_last_enq_ts` 已被 B 覆蓋，那系統算出的 latency 可能接近 `20 - 12 = 8`，但 A 真正的 queue residency 其實是 `20 - 10 = 10`。

這就是為什麼目前 `/proc` latency 應該被解讀為 **teaching metric / approximate aggregate metric（教學用近似聚合指標）**，而不是嚴格可追溯的 trace metric。

### 10.3 SHM 的 trace 實作步驟

SHM 模組沿用相同觀念：

- `st_wr`: 寫入次數
- `st_rd`: 讀取次數
- `st_lat_ns_total`: 累積延遲
- `st_last_wr_ts`: 最近一次寫入時間

在 `shm_stats_show()` 中輸出：

- `write_count`
- `read_count`
- `avg_latency_ns`
- `ring_used_slots`
- `ring_free_slots`
- `mmap_size_bytes`

### 10.4 `/proc` 為什麼適合這個專案？

因為這個專案目的是教學與 benchmark，不是生產環境的大規模 observability platform。

`/proc` 在本專案中的用途：

- 可直接由 `cat /proc/...` 讀取
- 可用 `seq_file` 穩定輸出文字格式
- 可直接被 `scripts/02_demo.sh` 與 `user/benchmark.c` 讀取
- 適合輸出當前計數器與佇列狀態

如果真的要做高精度 tracing，才會更傾向：

- tracepoints
- relay buffer
- perf ring buffer
- eBPF maps + user readers

### 10.5 Userspace timing trace

除了 kernel 端 `/proc` 統計，userspace 還有另一層 trace：

- `now_us()` 使用 `clock_gettime(CLOCK_MONOTONIC, ...)`
- `mq_demo.c`、`shm_demo.c` 用它量每次 enq/deq 的耗時
- `benchmark.c` 用它量 producer、consumer 與 wall-clock time

這層 trace 的定位是：

- 觀察 API 呼叫耗時
- 比較三條資料路徑的 userspace 端耗時
- 顯示結果給人看

### 10.6 Trace 與 benchmark 的關係

這個專案的 trace 並不是獨立存在，而是嵌進 benchmark 流程中：

1. benchmark 執行測試
2. benchmark 結束後讀 `/proc/mq_stats`
3. benchmark 結束後讀 `/proc/shm_stats`
4. 將 kernel latency 與 userspace throughput 並列呈現

這種設計讓學習者能同時看到：

- user-space 視角的吞吐量
- kernel-side 視角的 queue 內部統計

這就是一種 **cross-layer observability（跨層觀測）**。

### 10.7 這個 trace 功能實作上真正重要的點

若要一句話概括本專案 trace 的關鍵實作，最精準的描述是：

> 在核心資料路徑上插入最小必要的時間戳與計數器，透過 `atomic64_t + ktime_get() + /proc seq_file` 導出聚合統計，再由 userspace demo/benchmark 將統計與 throughput 一起顯示。

這個功能的核心組件是：

1. `ktime_get()`：取高解析單調時間
2. `atomic64_t`：安全累加跨路徑計數器
3. `WRITE_ONCE` / `READ_ONCE`：降低編譯器或 CPU 對共享欄位存取的危險重排
4. `proc_create` + `seq_file`：把統計導出到 `/proc`
5. `clock_gettime(CLOCK_MONOTONIC)`：userspace 補充觀測

---

## 11. 效能來源分析

### 11.1 Copy 成本

`copy_from_user()` / `copy_to_user()` 的成本不只是搬 64 bytes 這麼簡單，它還包含：

- 使用者位址檢查
- page fault handling 路徑可能性
- cache fill / eviction
- kernel/user 模式切換關聯成本

當訊息筆數高、每筆固定為 64 bytes 時，這些固定成本在總時間中的占比會上升。

### 11.2 Syscall 成本

每筆訊息做一次 `write()` 與一次 `read()`，等於每筆都要進出 kernel。即使 payload 很小，mode switch 仍然要付費。

在 `mmap` 路徑中，只有 setup 時有 syscall；資料平面（data plane）本身不再有每訊息 syscall。

### 11.3 Wait Queue 與 Spin 的差異

MQ 使用 wait queue：

- 好處是 queue 滿/空時不浪費 CPU
- 壞處是 sleep/wakeup/scheduler 成本存在

SHM mmap 路徑使用 spin-wait：

- 好處是延遲低、反應快
- 壞處是空轉時會耗 CPU

因此 `mmap` 路徑的高吞吐量不等於所有工作負載都應採用 busy-wait。若 producer 與 consumer 長時間不同步，`while` 迴圈會持續消耗 CPU 時間。

### 11.4 Cache locality（快取區域性）

共享記憶體的 ring buffer 讓資料常駐在共享頁面中，producer/consumer 反覆碰觸的是一小塊固定結構：

- `head`
- `tail`
- `data[slot]`

在本專案的資料路徑中，這會減少跨 kernel boundary 的資料搬運次數，並把 hot fields 限縮在固定共享區內。

---

## 12. 設計限制與工程注意事項

這一節不是批評，而是嚴格區分「教學實作」與「生產級實作」。

### 12.1 目前的 latency trace 不是精確 per-message correlation

前面已說明，`st_last_enq_ts` / `st_last_wr_ts` 只有單一全域時間戳。因此：

- 可用於總量與趨勢觀察
- 不適合當成精準事件對應的 latency trace

若要精確實作，必須為每個 slot 附帶 timestamp，或建立獨立事件 log。

### 12.2 固定 64-byte 訊息是假設，不是一般化訊息系統

本專案預設：

- `write()` 都以 64 bytes 為單位運作
- `read()` 也以 64 bytes 讀取

所以它是 benchmark-friendly 設計，不是通用任意大小訊息框架。

### 12.3 SHM syscall 寫入路徑的 spinlock 使用要特別理解

`shm_write()` 在 `spin_lock()` 保護區內呼叫 `copy_from_user()`。從正式核心工程角度看，這是一個需要明確標記的風險點，因為 `copy_from_user()` 可能涉及 page fault 路徑，而持有 spinlock 的區段原則上不應睡眠。

在這個專案裡，這是一個為了簡化展示資料路徑而留下的設計。若要往 production-grade kernel code 前進，常見做法是：

- 先把 user buffer 複製到臨時 kernel buffer
- 再在短小 critical section 內更新 ring

這樣能降低持鎖風險。

### 12.4 小於 64 bytes 的寫入語意需審慎看待

程式碼中若 `len < MSG_SIZE`，仍然可能以 64-byte message 的方式處理。因此專案實際期待的操作方式是：

- userspace 一律寫固定 64 bytes
- userspace 一律讀固定 64 bytes

也就是 benchmark 使用方式必須遵守模組假設。

### 12.5 `mmap` 路徑假設 SPSC

若要擴展成多 producer 或多 consumer，就不能只靠目前這組 head/tail + barrier 模型。那時需要重新考慮：

- atomic index updates
- compare-and-swap
- 多槽位狀態管理
- contention control

---

## 13. 如果把這份專案拿來教學，應該怎麼教

### 13.1 第一階段：先教資料路徑

先讓學生只看：

- MQ: `user -> kernel -> user`
- SHM mmap: `user <-> shared pages <-> user`

把 copy 次數講清楚，比先談抽象 IPC 類型更有效。

### 13.2 第二階段：教同步

再比較：

- `mutex + wait queue`
- `spinlock`
- `lock-free SPSC + barrier`

這時學生才會真正理解「為什麼快」不是一句 shared memory magically faster，而是同步策略也在改變。

### 13.3 第三階段：教 trace 與量測

最後再教：

- `ktime_get()` 與 `clock_gettime()`
- `/proc` 統計輸出
- 聚合統計與逐事件 trace 的差異

這能幫學生建立正確觀念：**有觀測，不等於有完整 trace；有平均值，不等於有每筆事件的真相。**

---

## 14. 總結

本專案把 Linux IPC 的三個核心議題放在同一個可讀、可測的實作中：

1. **資料怎麼走**
2. **同步怎麼做**
3. **效能怎麼量**

`mq_module` 代表的是「透過 kernel 中介搬運訊息」的 copy-based kernel-mediated model；`shm_module` 則用同一份共享 ring 結構，同時展示了：

- 還在 syscall/copy 世界中的 shared-memory-like queue
- 真正透過 `mmap()` 進入 zero-copy 世界的共享記憶體設計

而 trace 功能的作用，是以最少的插樁支撐整個教學與 benchmark：

- 用 `ktime_get()` 與 `atomic64_t` 在 kernel 端建立最小觀測面
- 用 `/proc` 匯出可讀統計
- 用 userspace 計時補足 end-to-end 視角

若要用一句話總結這份專案的技術本質，可以寫成：

> 這是一個以 Linux kernel module、character device、`kfifo`、`vmalloc`、`mmap`、ring buffer、memory barrier、`/proc` 統計與 pthread benchmark 為核心的 IPC 教學與效能對照實驗，重點在於把 copy path、syscall cost、同步模型與 trace/observability 的實作方式具體化。

---

## 15. 關鍵重點速記

1. `mq_module` 是自製 message queue，不是 POSIX mq。
2. `shm_module` 是自製 shared memory，不是 `shm_open()`/`shmget()`。
3. MQ 與 SHM syscall 路徑都有兩次 copy。
4. SHM mmap 路徑的關鍵優勢是 zero-copy 與 zero per-message syscall。
5. `vmalloc_to_pfn() + remap_pfn_range()` 是共享頁面映射的核心技術。
6. `head == tail` 表示 empty，`(head + 1) % cap == tail` 表示 full。
7. `volatile` 不等於同步保證，真正重要的是 memory barrier。
8. 本專案的 trace 是 `/proc` 聚合統計加上時間插樁，不是正式 tracepoint/ftrace/eBPF tracing。
9. `/proc` 輸出的 `avg_latency_ns` 是以單一最新時間戳推導出的聚合統計值，不是每筆訊息精準配對後的平均值。
10. 整個專案最值得學的不是 API 名稱，而是資料平面（data plane）與控制平面（control plane）如何拆開思考。
