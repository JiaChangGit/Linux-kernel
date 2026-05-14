# Linux IPC 基準測試：技術原理與核心實作解析報告

本報告旨在深入剖析 `linux-ipc-benchmark` 專案的底層設計。本專案不直接呼叫 Linux 既有的 IPC API，而是透過開發自定義核心模組（Kernel Modules），精確對照 **Message Queue (訊息佇列)** 與 **Shared Memory (共享記憶體)** 在 Linux 核心核心中的資料路徑差異。

---

## 1. 專案定位與核心概念

本專案的核心教學目標是揭示 **「為什麼共享記憶體比較快？」** 這個問題的底層答案。我們將原因拆解為三個可觀察的面向：
1.  **資料複製次數 (Data Copy Count)**：從使用者空間到核心空間的跨界開銷。
2.  **系統呼叫次數 (Syscall Overhead)**：行程切換與 Trap 進入核心的成本。
3.  **同步機制 (Synchronization)**：阻塞式等待 (Blocking) 與主動輪詢 (Polling) 的效能對比。

---

## 2. 核心技術與工具鏈

本專案運用了多項 Linux 核心開發的關鍵技術：

-   **核心環形緩衝區 (kfifo)**：利用 Linux 核心內建的無鎖 (Lock-free) FIFO 結構作為訊息佇列的基礎。
-   **虛擬記憶體管理 (vmalloc & mmap)**：動態配置非連續實體分頁，並透過 `remap_pfn_range` 將核心分頁映射至使用者空間。
-   **同步原語**：
    -   **Wait Queues**：用於 `mq_module` 的生產者/消費者阻塞式通知。
    -   **Spinlocks**：用於 `shm_module` 的 Syscall 路徑保護。
    -   **Memory Barriers**：用於 `mmap` 路徑的記憶體一致性屏障。
-   **統計觀測 (Observability)**：利用 `atomic64_t` 與 `procfs` 實作低負載的即時遙測系統。

---

## 3. 模組架構深層剖析

### 3.1 `mq_module`：基於 kfifo 的訊息佇列
這是一個標準的字元設備（Character Device），實作了阻塞式的訊息傳遞。

#### 資料流向 (Data Flow)：
1.  **生產者 (Producer)**：呼叫 `write()` -> 進入 `mq_write` -> `copy_from_user` 拷貝資料至核心 `kfifo` -> 喚醒等待中的讀取者。
2.  **消費者 (Consumer)**：呼叫 `read()` -> 進入 `mq_read` -> 從 `kfifo` 提取資料 -> `copy_to_user` 拷貝回使用者空間。

**關鍵代價**：每一筆訊息都必須跨越 **兩次** 使用者/核心邊界 (User/Kernel Boundary)，這是效能瓶頸的主因。

### 3.2 `shm_module`：支援 mmap 的共享記憶體
本模組實作了一個極為精巧的設計，同時提供兩種存取路徑進行效能對照。

#### A. Syscall 路徑 (`read`/`write`)：
為了公平對比，我們在共享記憶體上層封裝了標準 I/O 介面。雖然底層是共享記憶體，但透過 `copy_from_user` 存取，其效能表現會與 `mq_module` 接近，這證明了效能開銷主要來自於「拷貝」而非「佇列機制」。

#### B. Zero-Copy 路徑 (`mmap`)：
這是本專案的效能冠軍。
-   **核心實作**：使用 `vmalloc` 配置記憶體，並在 `shm_mmap` 函式中手動走訪分頁表（Page Table），利用 `remap_pfn_range` 將這些物理頁面直接掛載到使用者行程的虛擬位址空間。
-   **同步邏輯**：使用者空間直接操作 `head` 與 `tail` 指標，僅需配合 `__sync_synchronize()` (Memory Barrier) 即可確保資料順序，完全不需經過核心轉發。

---

## 4. 關鍵函式與實作細節

### 4.1 `copy_to_user` 與 `copy_from_user`
-   **用途**：在核心與使用者空間之間安全地搬移資料。
-   **底層細節**：這不是單純的 `memcpy`。它會檢查指標的合法性（防止存取核心位址）並處理分頁缺失 (Page Fault)。
-   **效能開銷**：涉及 TLB 刷新與快取污染 (Cache Pollution)，是傳統 IPC 的主要成本。

### 4.2 `remap_pfn_range` (共享記憶體的核心)
-   **原型**：`int remap_pfn_range(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn, unsigned long size, pgprot_t prot);`
-   **角色**：將實體頁面框號 (PFN) 映射到虛擬記憶體區域 (VMA)。
-   **實作難點**：由於 `vmalloc` 分配的記憶體在實體上可能不連續，我們必須在迴圈中逐頁呼叫此函式，精確建立對映。

### 4.3 記憶體屏障 (Memory Barriers)
在共享記憶體環形緩衝區中，必須確保「資料寫入」先於「指標更新」。
-   **核心端**：使用 `smp_wmb()` (Write Memory Barrier)。
-   **使用者端**：使用 `__sync_synchronize()`。
如果少了屏障，現代 CPU 的亂序執行（Out-of-Order Execution）可能會導致消費者讀到尚未更新完成的髒資料。

---

## 5. 橫向對比分析

| 特性 | Message Queue | SHM (Syscall) | SHM (Zero-Copy) |
| :--- | :--- | :--- | :--- |
| **資料複製次數** | 2 次 | 2 次 | **0 次** |
| **每筆訊息 Syscall** | 2 次 | 2 次 | **0 次** |
| **同步方式** | 核心 Wait Queue (睡眠/喚醒) | Spinlock (忙等/切換) | Userspace Polling (使用者空間輪詢) |
| **適用場景** | 流量不穩定、需要核心緩衝 | 測試/隔離對比 | 極高頻率、低延遲需求 |

---

## 6. 開發挑戰與除錯紀錄 (Troubleshooting)

### 6.1 快取偽共享 (False Sharing)
在初期開發中，`head` 與 `tail` 緊鄰在一起，導致生產者 CPU 與消費者 CPU 不斷爭奪同一個快取行（Cache Line），效能大幅下降。
-   **解決方案**：在 `struct shm_region` 中加入 `_pad[48]`，強行將兩個指標推到不同的快取行，吞吐量因此提升了約 30%。

### 6.2 核心版本相容性
Linux 6.4 之後，`class_create` 的 API 發生了變化（減少了一個參數）。本專案已針對最新核心版本進行調整，並在程式碼中標註了相容性差異。

### 6.3 鎖的選用
`mq_module` 涉及 I/O 阻塞，因此必須使用 `mutex` (可睡眠鎖)；而 `shm_module` 追求速度，在 Syscall 路徑中使用 `spinlock`，這反映了不同場景下對延遲與吞吐量的取捨。

---

## 7. 結論與未來展望

`linux-ipc-benchmark` 透過實作證明了 Shared Memory (mmap) 的壓倒性優勢。在 6.8 核心與 Ubuntu 24.04 的環境下，零拷貝路徑的吞吐量通常能達到傳統訊息佇列的 **5 倍以上**。

**未來延伸議題：**
- **Huge Pages 支援**：研究映射 2MB 或 1GB 的大頁面如何減少分頁表走訪次數。
- **lock-free 指令集**：導入 C11 `stdatomic.h` 與核心 `atomic_t` 的深層對比。
- **異質核心通訊**：將此架構延伸至不同處理器核心（如 AMP 架構）間的通訊模擬。
