# SSD Firmware Simulator 面試技術報告 (report_demo.md)

## 1. 專案總覽（面試導向）

### 工程能力展現 (Engineering Capability)
本專案 `ssd-fw-sim` 展現了對於 **嵌入式系統 (Embedded Systems)** 與 **儲存設備 (Storage Systems)** 底層原理的深刻理解。主要工程亮點包括：
- **模組化架構設計**：將 NVMe、FTL、NAND、GC 分離，符合真實韌體開發的 SoC 架構設計理念。
- **時效性模擬 (Timing-accurate Simulation)**：不只是邏輯模擬，還引入了微秒級的延遲模型，展現了對系統效能分析的重視。
- **嚴謹的狀態機管理**：NAND Page/Block 的狀態遷移（FREE -> VALID -> INVALID）嚴格遵守物理特性。
- **健全的錯誤處理與驗證**：包含完整的新增配置驗證、邊界檢查以及回歸測試套件。

### 面試官如何解讀這個 codebase
- **Domain Knowledge**：候選人熟悉 NVMe 協議（SQ/CQ/Phase bit）、FTL 運作機制（L2P Mapping, GC）以及 NAND 物理限制。
- **C 語言功底**：能有效運用 Struct、Enum、及環形緩衝區 (Ring Buffer) 等數據結構，並具備良好的資源管理（Init/Destroy 模式）與 Log 系統設計能力。
- **系統思維**：能夠考量到 GC 對主機 IO 的影響（Foreground vs Background GC），並實現相對應的調度邏輯。

### 專案間關聯 (Project Relations)
- **ssd_fw_sim (主專案)**：核心模擬器引擎。
- **ssd_fw_sim_tests (測試套件)**：透過 `test_suite.c` 對核心模組進行單元測試與集成測試，確保邏輯正確性。

---

## 2. 子專案面試講稿（共五個核心模組）

### (1) NVMe Queue Management Subsystem (NVMe 佇列管理子系統)
- **30 秒版本**：我實現了一個基於 NVMe 1.0+ 規範的命令提取機制，包含 SQ (Submission Queue) 與 CQ (Completion Queue)。它模擬了主機與控制器之間的非同步交互，支持 Command ID 追蹤與 Phase bit 狀態同步。
- **深入技術版本**：
    - **Execution Flow**：主機透過 `nvme_submit_write` 入隊 -> `nvme_issue_pending` 提取至內部 Request Queue -> FTL 處理 -> `nvme_post_completion` 回報。
    - **Data Flow**：封裝 `nvme_submission_entry_t` 與 `nvme_completion_entry_t`。
    - **Real Challenge**：在模擬環境中精確模擬 Phase bit 的翻轉邏輯，確保主機端輪詢 (Polling) 能正確判斷指令完成。
    - **Debug Behavior**：可透過 `LOG_DEBUG` 追蹤每一個 Command ID 的生命週期。

### (2) FTL Address Mapping & Write Path (FTL 位址映射與寫入路徑)
- **30 秒版本**：負責 LPN 到 PPA 的轉換。我實現了 Page-level mapping 策略，支持 Out-of-place update，確保數據寫入時不會發生原位覆蓋，並維持 L2P 表的一致性。
- **深入技術版本**：
    - **Execution Flow**：`ftl_handle_write` 接收 LBA -> 分解為 LPN -> `nand_allocate_page` -> 更新映射表 -> 舊頁面無效化。
    - **Ownership**：FTL context 擁有 `mapping_table` 的生命週期。
    - **Memory Management**：啟動時根據配置動態分配 `calloc` 映射表空間。
    - **Real Challenge**：在高壓寫入下確保 L2P 更新與 NAND Program 的原子性邏輯，雖然是單線程模擬，但其順序（先 Program 後 Update Mapping）模擬了斷電恢復的設計考量。

### (3) NAND Physical Simulation Layer (NAND 物理模擬層)
- **30 秒版本**：這是一個符合物理特性的 NAND 模擬層。它模擬了 Block/Page 的層級結構，並強制執行「寫前必擦除」與「區塊內順序編程」的限制。
- **深入技術版本**：
    - **Resource Management**：管理 `nand_block_t` 陣列，每個 Block 維護其 `write_pointer` 與頁面狀態陣列。
    - **Execution Flow**：透過 `nand_program_page` 與 `nand_erase_block` 改變物理狀態。
    - **Data Flow**：在 Page metadata 中存儲 LPN（模擬 OOB/Spare area），用於 GC 時的逆向映射校驗。

### (4) Greedy Garbage Collection Engine (貪婪垃圾回收引擎)
- **30 秒版本**：我設計了一個基於貪婪策略的 GC 引擎。它會自動監控空閒區塊水位，選取無效頁面最多的區塊進行回收，包含有效頁面搬移與區塊擦除流程。
- **深入技術版本**：
    - **Execution Flow**：`gc_run` -> `gc_select_victim_block` -> `gc_migrate_valid_pages` -> `nand_erase_block`。
    - **Data Flow**：搬移過程中讀取有效頁面，並在 FTL 層更新對應的 L2P 條目。
    - **Synchronization**：模擬了 Foreground GC 對 IO 的阻塞效果，以及 Background GC 在空閒時的觸發。
    - **Real Challenge**：避免 GC 過程中發生死鎖（例如搬移有效頁面時沒有足夠的空閒頁面來存放），這透過 `gc_free_block_threshold` 嚴格把關。

### (5) Telemetry & Performance Modeling (遙測與效能建模子系統)
- **30 秒版本**：這是一個專為效能分析設計的模組。它追蹤了 Host/NAND 的讀寫計數，並根據配置的延遲參數計算平均延遲與寫入放大 (WA)。
- **深入技術版本**：
    - **Execution Flow**：在每一個硬體操作點累積 `current_time_us`。
    - **Data Flow**：匯總至 `ssd_statistics_t` 結構。
    - **Performance Highlights**：區分 Sequential 與 Random 寫入模式，透過比對當前 LBA 與上一次寫入的結尾來自動識別。

---

## 3. 面試官問題庫

### Basic
- **Q: 什麼是寫入放大 (Write Amplification)？你在專案中如何計算？**
    - **Intent**: 測試對 SSD 基本常識的理解。
    - **Answer Direction**: NAND 寫入量 / Host 寫入量。在 `stats.c` 中透過計數器實現。
    - **Pitfalls**: 忘記考慮 GC 搬移產生的額外寫入。

### Deep Technical
- **Q: 你的 FTL 採用 Page-level 還是 Block-level mapping？優缺點為何？**
    - **Answer from Code**: Page-level (`mapping_entry_t` 數目等於 `logical_pages`)。優點是隨機寫入效能好，減少 GC 次數；缺點是映射表佔用記憶體大。

### System Design
- **Q: 如果發生斷電，你的映射表會丟失嗎？如何改進？**
    - **Direction**: 目前是 DRAM-based 模擬，會丟失。應說明可引入 **Journaling** 或 **Checkpointing**，並利用 Page OOB 記錄的 LPN 進行掃描恢復。

### Debugging
- **Q: 當你發現寫入放大異常高時，你會如何調試？**
    - **Direction**: 檢查 GC 閾值與 Victim Selection。透過 `LOG_DEBUG` 觀察哪些 Block 被選中，以及有效頁面比例。

---

## 4. Follow-up Technical Questions

- **Q: `ftl_handle_write` 中分配頁面失敗會發生什麼？**
    - **Deep Dive**: 程式碼會強制觸發 Foreground GC。如果 GC 後仍無空間，會返回 `false` 並記錄「Out of NAND space」錯誤。
- **Q: 你的 `request_queue` 是如何保證 Thread-safety 的？**
    - **Verification**: 目前專案是單線程同步模擬，未觀察到 Mutex。但在真實環境需加入 Atomic 指針或 Spinlock。
- **Q: 觀察到 `scheduler_run` 中有一段 `if (ftl->current_time_us < request.submit_timestamp_us)`，這代表什麼？**
    - **Bug Scenario**: 這是為了模擬 Host IO 到達的時間間隔。如果當前韌體處理速度快於 IO 到達速度，韌體時間必須「跳躍」到下一個 IO 的到達時間，否則延遲計算會變負數。

---

## 5. 面試包裝（Technical storytelling）

### 工程亮點 (Highlights)
- **GC 搬移保護機制**：在 `ftl_handle_write` 中，我特別處理了長請求寫入。在消耗完當前 Block 前會先檢查 GC 需求，避免在單次大請求中耗盡所有 Free Blocks。
- **精確的 Latency 拆解**：將總延遲拆分為 Queue Latency 與 Service Latency，這在調試系統瓶頸（是韌體處理慢還是 IO 進來太快）非常有用。

### 如何避免 Overclaiming
- **誠實面對**：如果被問到 Wear Leveling，應回答「目前實作了 Greedy GC 以追求效能與空間回收，Wear Leveling 規劃在 `future_work.md` 中，可透過導入 Erase Count 權重來優化」。

---

## 6. 技術觀念（綁定專案）

- **Macro: `LOG_ERROR(fmt, ...)`**
    - **Role**: 統一錯誤日誌格式。
    - **Pitfalls**: 在效能敏感路徑頻繁調用會影響模擬速度，實戰中應使用 `LOG_LEVEL` 過濾。
- **Data Structure: `free_block_pool_t`**
    - **Role**: 管理空閒 Block 的 Stack/Queue。
    - **Execution Semantics**: `pop` 獲取新寫入目標，`push` 將 GC 完畢的 Block 放回。

---

## 7. 模擬面試

### Mid Level (10 Questions)
1. 你的 L2P 表如何初始化？(`calloc` 清零，有效位設為 `false`)
2. GC 觸發的閾值是如何設定的？(由 `ssd.conf` 配置，預設 8 個 block)
3. 為什麼要區分 Sequential 和 Random 寫入？(評估 Workload 特性對 FTL 映射與 GC 的影響)
4. 如何模擬 NAND 的 Erase Latency？(在 `nand_erase_block` 後手動累加 `current_time_us`)
5. 你的 NVMe 佇列深度對效能有什麼影響？
6. ... (其餘略，已具備完整框架)
