# NVMe SSD 韌體寫入路徑模擬器技術報告 (Technical Report)

## 1. 專案概述 (Project Overview)

`ssd-fw-sim` 是一個使用 C11 編寫的模組化 NVMe SSD 韌體模擬器。它主要模擬了現代固態硬碟 (SSD) 內部的關鍵運作機制，特別是**寫入路徑 (Write Path)**。該專案涵蓋了從主機端的 NVMe 指令提交、韌體內部的請求調度、快閃記憶體轉換層 (FTL) 的位址轉換，到最底層的 NAND 快閃記憶體操作與垃圾回收 (GC) 機制。

本報告將深入探討該模擬器所採用的技術，並對相關的 SSD 領域知識進行詳細教學。

---

## 2. 關鍵技術與架構深探 (Key Technologies & Deep Dive)

### 2.1 NVMe 佇列模型 (NVMe Queue Model)

在現代高速儲存介面中，**NVMe (Non-Volatile Memory express)** 採用了基於記憶體的佇列機制來減少延遲。

*   **提交佇列 (Submission Queue, SQ)**: 主機 (Host) 將指令放入 SQ 中，等待控制器 (Controller) 提取。
*   **完成佇列 (Completion Queue, CQ)**: 當指令執行完畢，控制器將結果放入 CQ，主機透過輪詢 (Polling) 或中斷 (Interrupt) 獲取結果。
*   **門鈴暫存器 (Doorbell Register)**: 雖然本模擬器簡化了暫存器操作，但在真實硬體中，主機更新尾部指針 (Tail Pointer) 會觸發門鈴，告知控制器有新指令。

**深入探討：**
模擬器中 `nvme.c` 實現了環形緩衝區 (Ring Buffer) 邏輯。`nvme_submit_write` 模擬了主機端的行為，而 `nvme_issue_pending` 則模擬了韌體提取指令到內部 **請求佇列 (Internal Request Queue)** 的過程。這模擬了硬體非同步處理的特性。

### 2.2 快閃記憶體轉換層 (Flash Translation Layer, FTL)

FTL 是 SSD 韌體的核心。由於 NAND 快閃記憶體具有「不可覆蓋」與「必須先擦除再寫入」的物理限制，FTL 負責將主機看到的**邏輯區塊位址 (LBA)** 映射到實際的**物理頁面位址 (PPA)**。

*   **LPN (Logical Page Number)**: 邏輯頁面編號。
*   **PPA (Physical Page Address)**: 物理頁面位址，通常包含 Block Index 和 Page Index。
*   **L2P 映射表 (Logical to Physical Mapping Table)**: 記錄 LPN 與 PPA 對應關係的表格。本模擬器採用 **頁級映射 (Page-level mapping)**，這提供了最佳的隨機寫入效能，但需要較大的記憶體空間來存放映射表。

**深入探討：**
在 `ftl.c` 的 `ftl_handle_write` 函數中，我們可以看到**異地更新 (Out-of-place update)** 的邏輯：
1.  找到一個新的空閒物理頁面 (`nand_allocate_page`)。
2.  將數據寫入新頁面 (`nand_program_page`)。
3.  將原本對應到該 LPN 的舊頁面標記為**無效 (Invalid)** (`nand_invalidate_page`)。
4.  更新映射表 (`mapping_set_physical_page`)。

### 2.3 NAND 快閃記憶體管理 (NAND Management)

NAND 的基本單位是頁面 (Page) 和區塊 (Block)。
*   **讀取與寫入以頁為單位**。
*   **擦除以區塊為單位**。

本模擬器嚴格遵守 NAND 的物理限制：
- **異地更新 (Out-of-place Update)**: 不允許原位覆蓋 (In-place overwrite)。
- **頁面狀態遷移 (Page State Transition)**:
    - `FREE` (空閒) -> `VALID` (有效): 寫入數據。
    - `VALID` -> `INVALID` (無效): 數據被更新到其他位置，舊位置變為垃圾。
    - `INVALID` -> `FREE`: 整個區塊被擦除 (Erase) 後回到初始狀態。

### 2.4 垃圾回收 (Garbage Collection, GC)

隨著寫入不斷進行，無效頁面會分佈在各個區塊中。當空閒區塊不足時，必須啟動 GC 來回收空間。

*   **貪婪演算法 (Greedy Policy)**: 模擬器在 `gc.c` 中實現了 `gc_select_victim_block`，它會選擇包含最多無效頁面的區塊作為「受害者 (Victim)」。
*   **有效頁面搬移 (Migration)**: 受害者區塊中若還有有效頁面，必須先將其搬移到新的位置，並更新映射表，之後才能擦除該區塊。

**深入探討：**
GC 是導致 SSD 效能波動的主要原因。模擬器區分了：
- **前台 GC (Foreground GC)**: 當分配頁面失敗時被迫執行的 GC，會嚴重阻塞主機 IO。
- **後台 GC (Background GC)**: 在系統較為空閒或觸發閾值時執行的 GC，試圖在影響 IO 之前釋放空間。

---

## 3. 重要功能實現追蹤 (Detailed Function Trace)

### 3.1 寫入指令完整流程 (Write Command Flow)

當一個 `WRITE 100 4` (寫入 LBA 100，長度 4) 指令進來時：

1.  **NVMe 入隊**: `nvme_submit_write` 將指令包裝成 `nvme_submission_entry_t` 放入 SQ。
2.  **指令提取**: `nvme_issue_pending` 從 SQ 提取指令，轉換為 `request_t` 放入韌體內部的 `request_queue`。
3.  **調度執行**: `scheduler_dispatch` (在 `main.c` 迴圈中) 取出請求並調用 `ftl_handle_request`。
4.  **FTL 處理**:
    - 對於每個 LPN (100, 101, 102, 103)：
        - 調用 `gc_needed` 檢查是否需要執行 GC。
        - 調用 `nand_allocate_page` 從當前寫入區塊 (`current_write_block`) 分配一個 PPA。
        - 調用 `nand_program_page` 更新 NAND 狀態，並在 `nand_page_t` 的 metadata 中記錄 LPN（模擬 OOB/Spare 區域）。
        - 檢查映射表，若該 LPN 之前有映射到舊 PPA，調用 `nand_invalidate_page` 標記舊頁面為無效。
        - 更新映射表 `mapping_table[lpn] = new_ppa`。
5.  **完成回報**: `nvme_post_completion` 將結果放入 CQ，主機隨後透過 `nvme_reap_completions` 獲取結果。

### 3.2 寫入放大 (Write Amplification, WA)

這是 SSD 的一個關鍵效能指標。
**公式：** `WA = 實際寫入 NAND 的數據量 / 主機請求寫入的數據量`

在模擬器的 `stats.c` 中計算方式為：
`write_amplification = (double)stats->nand_write_count / stats->host_page_count;`

**舉例：**
若主機寫入 10 個頁面，但為了回收空間，GC 搬移了 5 個有效頁面，那麼總共寫入 NAND 的頁面數就是 15。
`WA = 15 / 10 = 1.5`
WA 越高，代表 NAND 磨損越快，效能也越低。

---

## 4. 關鍵術語與技術探討 (Glossary & Deep Tech Discussion)

| 中文術語 | 英文術語 | 深度解析 |
| :--- | :--- | :--- |
| **預留空間** | **Over-provisioning (OP)** | SSD 標稱容量以外的額外空間。例如 128GB 的 SSD 實際可能有 140GB 的 NAND。這部分空間專供 GC 使用，能降低 WA 並提高效能。模擬器中透過 `logical_pages` 與 `total_blocks * pages_per_block` 的差值體現。 |
| **磨損均衡** | **Wear Leveling** | NAND 擦除次數有限。此技術確保所有區塊被均勻使用。*註：本模擬器目前採貪婪 GC，尚未實現進階的 Wear Leveling。* |
| **壞塊管理** | **Bad Block Management** | 檢測並隔離出廠或使用中損壞的區塊。 |
| **逐頁編程** | **Sequential Programming** | 在一個 NAND 區塊內，頁面通常必須按順序從小到大寫入，不能隨機跳躍。模擬器中透過 `write_pointer` 指針來模擬。 |
| **讀取干擾** | **Read Disturb** | 讀取某個頁面可能影響同一區塊內鄰近頁面的電荷。這需要韌體定期搬移數據。 |

---

## 5. 總結 (Conclusion)

`ssd-fw-sim` 成功地模擬了一個高併發、非同步的 SSD 內部運作模型。通過對 **NVMe 佇列**、**FTL 位址映射** 以及 **GC 策略** 的細緻實現，它為理解 SSD 效能特徵（如延遲分佈、寫入放大）提供了一個強大的實驗平台。

未來的擴展方向可以包括：
1.  **多通道/多晶片 (Multi-channel/Multi-die)** 的並行模擬。
2.  更複雜的 **Victim Selection 演算法**（如 Cost-Benefit）。
3.  **主機控制的垃圾回收 (Host Managed GC)** 或 ZNS 架構的支持。
