# Linux IPC Benchmark API 技術報告

本報告針對 `/linux-ipc-benchmark` 子專案進行深度 Codebase Trace 與架構分析。內容完全基於實體原始碼 (`kernel/*.c`, `user/*.c`)、標頭檔 (`common.h`) 及腳本的實際實作。

---

## 第一階段：Codebase Trace (程式碼追蹤)

### 1. Project Structure (專案結構)

- **Source Files (Kernel)**:
    - `kernel/mq_module.c`: 實作基於 `kfifo` 的訊息佇列 (Message Queue) 核心模組。
    - `kernel/shm_module.c`: 實作基於 `vmalloc` 緩衝區與 `mmap` 的共享記憶體 (Shared Memory) 核心模組。
- **Source Files (Userspace)**:
    - `user/benchmark.c`: 三種 IPC 機制的效能對比基準測試工具。
    - `user/mq_demo.c`: 訊息佇列的功能展示程式。
    - `user/shm_demo.c`: 共享記憶體的功能展示程式。
- **Header Files**:
    - `user/common.h`: 定義核心與使用者空間共用的常數 (`MSG_SIZE`, `RING_CAPACITY`) 與資料結構 (`shm_region_t`)。
- **Build System**:
    - **目前程式碼中未觀察到**單獨的 `Makefile`。根據 `scripts/01_setup.sh` 判斷，驅動使用 Kbuild 編譯，使用者程式則直接呼叫 `gcc`。
- **Scripts**:
    - `01_setup.sh`: 編譯並載入模組，建立 `/dev/` 裝置與權限。
    - `02_demo.sh`: 執行 MQ 與 SHM 的功能演示。
    - `03_benchmark.sh`: 執行效能壓測。
    - `04_cleanup.sh`: 移除模組與清理裝置。

### 2. Semantic Element Extraction (語義要素萃取)

- **Kernel API (IPC 核心)**:
    - `kfifo_in`, `kfifo_out`: 訊息佇列的緩衝管理。
    - `wait_event_interruptible`, `wake_up_interruptible`: 實作 MQ 的阻塞式讀寫。
    - `remap_pfn_range`: 將核心 `vmalloc` 頁面映射至使用者空間。
    - `copy_from_user`, `copy_to_user`: 核心與使用者空間之間的資料交換。
- **Synchronization Primitives**:
    - `mutex` (MQ): 保護 `kfifo` 存取。
    - `spinlock_t` (SHM Syscall Path): 保護環形緩衝區指標。
    - `atomic64_t`: 統計效能數據與延遲。
    - `smp_wmb`, `smp_rmb` (Kernel) / `__sync_synchronize` (User): SHM 零拷貝路徑的記憶體屏障 (Memory Barrier)。
- **Memory Management**:
    - `vmalloc`: 配置分頁不連續但虛擬位址連續的核心緩衝區。
    - `mmap`: 使用者空間獲取核心緩衝區指標。

### 3. API / Macro Inventory (依照功能分類)

| 分類 | 元素名稱 | 類型 | 呼叫位置 | 用途 |
| :--- | :--- | :--- | :--- | :--- |
| **Initialization** | `mq_init` | function | `mq_module.c` | 註冊 `/dev/mq_ipc` 與 `/proc/mq_stats`。 |
| | `shm_init` | function | `shm_module.c` | 配置 `vmalloc` 記憶體並註冊裝置。 |
| **Execution Path** | `mq_write` | function | `mq_module.c` | **2 拷貝路徑**：ubuf -> kb -> kfifo。 |
| | `shm_write` | function | `shm_module.c` | **2 拷貝路徑**：ubuf -> ring buffer (透過系統呼叫)。 |
| | `shm_mmap` | function | `shm_module.c` | **0 拷貝建立**：設定頁面映射，後續存取不經系統呼叫。 |
| **Stats** | `mq_stats_show` | callback | `mq_module.c` | 顯示平均延遲與 FIFO 使用率。 |
| **Sync (User)** | `__sync_synchronize` | builtin | `benchmark.c` | 確保資料寫入與 head 指標更新的順序。 |

### 4. Call Graph (呼叫圖譜)

- **Message Queue Path**:
    `user:write()` -> `kernel:mq_write` -> `wait_event_interruptible` (若滿則睡) -> `mutex_lock` -> `kfifo_in` -> `wake_up_interruptible` (喚醒讀者) -> `copy_from_user`。

- **Shared Memory (Syscall Path)**:
    `user:write()` -> `kernel:shm_write` -> `spin_lock` -> `copy_from_user` -> `smp_wmb` (更新 head)。

- **Shared Memory (Mmap Path)**:
    1. **Setup**: `user:mmap()` -> `kernel:shm_mmap` -> `vmalloc_to_pfn` -> `remap_pfn_range`。
    2. **Execution**: `user:memcpy` -> `__sync_synchronize` -> `shm->head++` (全使用者空間操作)。

### 5. Struct / Resource Tracing (資源追蹤)

- **`shm_region_t` (定義於 `common.h`)**:
    - **作用**: 使用者與核心共享的 Layout。
    - **成員**: `head` (生產者擁有), `tail` (消費者擁有), `data` (環形資料區)。
    - **對齊**: 使用 `_pad[48]` 將 `head/tail` 分隔在不同 Cache Line (64B)，避免**偽共享 (False Sharing)** 造成的效能損耗。
- **`g_shm` (Kernel)**:
    - **配置**: `vmalloc(SHM_BUF_SIZE)`。
    - **生命週期**: 模組載入時配置，卸載時 `vfree`。

### 6. Execution Trace (執行流程)

```text
[Initialization]
01_setup.sh -> insmod mq_module.ko -> insmod shm_module.ko
            -> 建立 /dev/mq_ipc, /dev/shm_ipc

[Runtime (Benchmark)]
1. Open Devices & Mmap SHM
2. Create Threads (Producer & Consumer)
3. Barrier Sync
4. Loop:
   a. MQ Test: write()/read() syscalls (Blocking)
   b. SHM Syscall Test: write()/read() syscalls (Non-blocking spin)
   c. SHM Mmap Test: Pointer access (Spin-wait)
5. Report Stats & /proc Summary
```

---

## 第二階段：Architecture / API Technical Report

### 1. API 語義與執行特性分析 (Execution Semantics)

本專案的核心在於透過三種不同的資料路徑展示 Linux 系統設計的演進：

- **MQ 路徑 (kfifo + blocking)**：
    - **語義**：標準的訊息傳遞模型。核心負責同步與緩衝管理。
    - **行為**：當佇列滿或空時，程序會進入休眠 (`wait_queue`)。這對 CPU 較友善，但涉及頻繁的 Context Switch。
- **SHM 系統呼叫路徑 (Spinlock)**：
    - **語義**：將共享記憶體視為傳統檔案。
    - **行為**：雖然使用了 `copy_from_user`，但與 MQ 的差異在於核心內部使用 `spinlock` 而非 `mutex`。若空間不足，直接回傳 `EAGAIN` 或 `ENOSPC` 而不休眠，這在競爭激烈時會顯著增加系統負擔。
- **SHM Mmap 路徑 (Zero-Copy)**：
    - **語義**：直接記憶體存取。核心僅參與初始的映射建立。
    - **行為**：使用者空間執行續透過 `volatile` 指標與記憶體屏障進行手動同步。這是效能最高的方案，因為它徹底消除了核心拷貝與系統呼叫的開銷。

### 2. 記憶體流動與所有權轉移 (Memory Flow & Ownership)

- **MQ 所有權**：資料的所有權在 `write` 呼叫後即轉移給核心 (`kfifo`)。
- **SHM 所有權**：`shm_region_t` 的所有權是**共享且分割的**。
    - **生產者**：寫入 `data[head]` 並更新 `head`。
    - **消費者**：讀取 `data[tail]` 並更新 `tail`。
    - 這種分割設計（配合 Cache Line Padding）是高效能 IPC 的標準實作，確保了生產者與消費者在大多數時間內不會在同一硬體快取行上產生競爭。

### 3. 同步行為分析 (Synchronization Behavior)

專案展示了三種層級的同步：
1.  **Heavy (MQ)**：使用核心信號量/互斥鎖 (`mutex`) 與等待佇列。適合大數據量、低頻率的傳輸。
2.  **Light (SHM Syscall)**：使用核心自旋鎖 (`spinlock`)。適合極短時間的互斥。
3.  **Ultra-Light (SHM Mmap)**：使用硬體層級的記憶體屏障 (`__sync_synchronize`)。生產者在寫入資料後才移動 `head`，消費者在偵測到 `head` 移動後才讀取資料，保證了資料的可見性 (Visibility)。

### 4. 技術選型差異比較

| 特性 | MQ (kfifo) | SHM (Syscall) | SHM (Mmap) |
| :--- | :--- | :--- | :--- |
| **資料拷貝次數** | 2 | 2 | 0 |
| **系統呼叫頻率** | 每訊息 1 次 | 每訊息 1 次 | 僅初始化 1 次 |
| **核心參與度** | 高 (管理緩衝與排程) | 中 (僅負責拷貝與同步) | 極低 (僅負責頁面映射) |
| **同步代價** | 高 (Context Switch) | 中 (Spinlock) | 極低 (Memory Barrier) |

### 5. 潛在風險與效能陷阱 (Potential Bug/Risk)

- **使用者空間自旋 (Spin-waiting)**：`benchmark.c` 的 `mmap_worker` 採用 `do { head = shm->head; ... } while (next == shm->tail);` 的死循環。若生產者/消費者被系統調度走，另一方會 100% 佔用 CPU 核心空轉。在生產環境中應考慮加入 `usleep(0)` 或 `pause()`。
- **緩衝區大小限制**：`RING_CAPACITY` 為 512。在高吞吐量下，若讀寫速度不匹配，很快會觸發自旋或 `EAGAIN`，效能會受限於最慢的一方。
- **頁面映射安全**：`remap_pfn_range` 將核心記憶體直接暴露給使用者。若使用者程式崩潰或惡意修改 `capacity` 等元數據，可能導致核心模組運算錯誤（雖本範例未做複雜運算，但在 BSP 開發中需謹慎）。

---
**結論**：`/linux-ipc-benchmark` 透過極具教育意義的程式碼結構，清晰地揭示了 Linux IPC 的效能瓶頸所在：**「系統呼叫開銷」與「核心/使用者空間拷貝」**。其 SHM Mmap 實作是現代高效能系統（如 DPDK、ZeroMQ）底層技術的微縮版。
檔案分析時間：2026-05-17
分析者：Gemini CLI
