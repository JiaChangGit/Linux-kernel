# SSD Firmware Simulator API 與執行語意技術報告 (`report_ssd_api.md`)

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

## 2. Codebase Trace 與 語義元素分析

### 2.1 專案結構 (Project Structure)
- **Source Files (`src/`)**: 包含 `main.c` (入口點與 Trace Replay)、`ftl.c` (核心邏輯)、`nvme.c` (Queue 管理)、`nand.c` (物理模擬)、`gc.c` (回收機制) 等。
- **Header Files (`include/`)**: 定義各模組的 Data Structure 與 API Contract。
- **Build System**: `Makefile` 負責編譯與連結。
- **Scripts**: `scripts/gen_trace.py` 用於生成測試用 Trace。

### 2.2 語義元素提取 (Semantic Element Extraction)
#### # Direct Observation (直接觀察)
- **API**: 例如 `ftl_handle_request`, `nvme_submit_write`, `nand_program_page`。
- **Macro**: `LOG_ERROR`, `LOG_DEBUG` (定義於 `common.h`)，用於統一日誌規範。
- **Memory Management**: 主要透過 `calloc`/`malloc` 在 `init` 階段分配，並在 `destroy` 階段釋放。
- **Execution Model**: 單執行緒同步模擬 (Single-threaded Synchronous Simulation)，透過 `current_time_us` 累加延遲。

#### # Conservative Inference (保守推論)
- **Synchronization**: 目前程式碼中未觀察到 Mutex 或 Spinlock，推論此模擬器專注於邏輯驗證而非多執行緒效能。
- **Communication Mechanism**: 模組間透過 Struct 指標進行直接調用，模擬 SoC 內部的 Function Call。

### 2.3 API 清單與功能分類 (API Inventory)
| 分類 | API 名稱 | 定義位置 | 用途 (基於 Code) | 關聯 Struct |
| :--- | :--- | :--- | :--- | :--- |
| **Initialization** | `ftl_init` | `ftl.c` | 初始化 FTL Context、Mapping Table 與 NAND | `ftl_context_t` |
| **Execution Path** | `scheduler_run` | `scheduler.c` | 驅動 Request Queue 處理流程 | `request_queue_t` |
| **Lifecycle** | `nand_destroy` | `nand.c` | 釋放 NAND 模擬層分配的記憶體 | `nand_ssd_t` |
| **GC Path** | `gc_run` | `gc.c` | 執行 Victim 選擇、數據搬移與 Block 擦除 | `ftl_context_t` |
| **Performance** | `stats_update_request` | `stats.c` | 更新延遲統計資訊 | `ssd_statistics_t` |
| **Config** | `ssd_config_load_file` | `config.c` | 解析 `key=value` 設定檔 | `ssd_config_t` |
| **Config** | `ssd_config_validate` | `config.c` | 檢查 geometry / threshold 合法性 | `ssd_config_t` |
| **Queue** | `request_queue_enqueue` | `request.c` | 內部 firmware RQ 入隊 | `request_queue_t` |
| **NVMe** | `nvme_has_pending` | `nvme.c` | SQ/RQ/CQ 任一非空 | `nvme_controller_t` |
| **Block pool** | `free_block_pool_get_min_erase_block` | `block_manager.c` | 自 pool 選 erase 最小 block | `free_block_pool_t`（**GC 未呼叫**） |

### 2.4 執行追蹤與流程圖 (Execution Trace & Flow Chart)
#### # Initialization Flow (初始化流程)
```text
main()
└── ssd_config_init_default() [載入預設配置]
└── request_queue_init() [分配內部 Request Queue]
└── nvme_controller_init() [分配 SQ/CQ 記憶體]
└── ftl_init()
    ├── nand_init() [分配 Block/Page 陣列]
    ├── mapping_table_init() [分配 L2P 表]
    └── free_block_pool_init() [初始化空閒區塊池]
```

#### # Runtime Flow - Host Write Path (運行時寫入路徑)
```text
main() -> replay_trace()
└── nvme_submit_write() [Host 指令入隊 SQ]
└── service_nvme_pipeline()
    ├── nvme_issue_pending() [從 SQ 提取至 Request Queue]
    └── scheduler_run()
        └── ftl_handle_request()
            └── ftl_handle_write()
                ├── gc_needed() ? -> gc_run() [回收機制]
                ├── nand_allocate_page() [獲取新 PPA]
                ├── nand_program_page() [物理寫入]
                └── mapping_set_physical_page() [更新 L2P]
    └── nvme_reap_completions() [Host 確認 CQ 完成]
```

#### # Cleanup Flow (資源回收流程)
```text
main()
└── ftl_destroy() -> nand_destroy() [釋放 NAND 與 FTL 資源]
└── nvme_controller_destroy() [釋放 SQ/CQ]
└── request_queue_destroy() [釋放 Queue 記憶體]
```

---

## 3. 架構與 API 技術深入分析 (Architecture & Execution Semantics)

### 3.1 資源生命週期與所有權 (Resource Lifecycle & Ownership)
- **Ownership**: `ftl_context_t` (全域變數 `g_ftl`) 擁有 `mapping_table` 與 `lpn_write_count` 的生命週期。
- **Allocation/Release**: 在 `ftl_init` 中使用 `calloc` 分配，`ftl_destroy` 中執行 `free`。這體現了「誰分配，誰釋放」的資源管理原則。

### 3.2 執行語義 (Execution Semantics)
- **時效性同步**: `scheduler_run` 透過 `if (ftl->current_time_us < request.submit_timestamp_us)` 確保模擬時間不會超前 Host Request 發出的時間。
- **Phase Bit 機制**: 在 `nvme_post_completion` 中，`controller->cq_phase ^= 1U` 的翻轉模擬了真實 NVMe 控制器與 Host 之間的 Status 同步機制。

### 3.3 狀態遷移追蹤 (State Transition Tracing)
- **NAND Page State**: `NAND_PAGE_FREE` (初始) -> `NAND_PAGE_VALID` (`nand_program_page`) -> `NAND_PAGE_INVALID` (`nand_invalidate_page`) -> `NAND_PAGE_FREE` (`nand_erase_block`)。
- **GC Trigger**: `gc_needed` 根據 `free_block_pool_count` 與 `config->gc_free_block_threshold` 決定觸發點。

### 3.4 比較分析 (Comparative Analysis)
- **Dispatch Model 比較**:
  - **Pull-based Model**: 本專案採用 `nvme_issue_pending` 主動從 SQ 提取指令至內部 `request_queue`，再由 `scheduler_run` 消耗。這種設計模擬了真實韌體中，「控制器主動 Fetch 指令」而非「由 Host 直接 Push」的行為。
  - **對比**: 較簡單的模擬器可能直接在 `submit` 時執行，但本專案透過 `request_queue` 建立了緩衝與解耦，更貼近 SoC 真實運作。
- **Resource Management Model**:
  - **Explicit Ownership**: 雖然 `g_ftl` 是全域變數，但其內部的 `mapping_table` 與 `lpn_write_count` 有明確的分配與回收路徑。
  - **對比**: 相比於使用靜態陣列，動態分配 (`calloc`) 允許根據 `ssd.conf` 配置靈活調整模擬規模，展現了良好的工程彈性。

### 3.5 Debug / Risk Analysis (風險分析)
- **GC 策略**: 目前採用 **Greedy Strategy** (選取 `invalid_page_count` 最高的 Block)。
  - **優點**: 每次回收能釋放最多的空閒空間。
  - **風險**: 缺乏 Wear Leveling，可能導致特定 Block 被頻繁擦除。
- **潛在風險**:
  - **Memory Management**: 若 `config` 中的參數極大，`calloc` 可能失敗，程式碼中有進行回傳值檢查，展現了對 Robustness 的考量。
  - **Concurrency**: 雖然目前是單執行緒，但若未來擴展至多執行緒，`g_ftl` 全域變數將面臨 Race Condition。

---

## 4. 子專案面試講稿（共五個核心模組）

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
6. `nvme_reap_completions` 做什麼？（僅遞增 `cq_head`、遞減 `cq_count`，不檢查 phase）
7. `make test` 跑哪些案例？（見下方 §8 補充）

---

## 8. 補充：Codebase Trace（增量，依現有原始碼）

> 本節在既有章節基礎上補齊**可直接從程式碼驗證**、但前文未逐項列出的元素。未出現在源碼中的機制一律標為「未觀察到」。

### 8.1 Build System 與測試入口

#### # Direct Observation

| 項目 | 位置 | 行為 |
|------|------|------|
| 主程式目標 | `Makefile` → `ssd_fw_sim` | 連結 `src/*.c`（含 `main.c`） |
| 測試目標 | `Makefile` → `ssd_fw_sim_tests` | `tests/test_suite.c` + 除 `main.c` 外之 core 物件 |
| 編譯旗標 | `CFLAGS` | `-std=c11 -Wall -Wextra -Werror -pedantic -O2 -Iinclude` |
| 執行測試 | `make test` | 建置並執行 `./ssd_fw_sim_tests` |

#### # Conservative Inference

- 測試程式與 `main.c` 共用 `service_nvme_pipeline` 的等價邏輯（測試內為靜態函式 `service_pipeline`），用於在無 trace 檔時驗證 NVMe/FTL 管線。

### 8.2 `main.c`：CLI 與 `service_nvme_pipeline`

#### # Direct Observation

`main` 支援參數（`main.c`）：

- 第一個非選項參數：trace 路徑（必填）
- `--config <path>`：呼叫 `ssd_config_load_file`
- `--csv <path>`：成功後 `stats_export_csv`
- 其餘多餘參數：`LOG_WARN` 後忽略

`service_nvme_pipeline` 固定順序：

1. `nvme_issue_pending(controller, request_queue)`
2. 若 RQ 非空 → `scheduler_run(ftl, request_queue, controller)`；回傳 false 則整條 pipeline 失敗
3. `nvme_reap_completions(controller)`

`replay_trace` 在每次 submit 後呼叫 pipeline；若 `nvme_cq_is_full`，額外 `nvme_reap_completions` 一次。

### 8.3 `config.c`：可載入鍵與驗證規則

#### # Direct Observation

`ssd_config_init_default` 預設（`config.c`）：`total_blocks=128`, `pages_per_block=64`, `logical_pages=4096`, `request_queue_depth=256`, `gc_free_block_threshold=8`, `read_latency_us=50`, `program_latency_us=200`, `erase_latency_us=1500`, `trace_inter_arrival_us=10`。

`ssd_config_load_file` 接受鍵（未知鍵回傳 -1）：

`total_blocks`, `pages_per_block`, `logical_pages`, `request_queue_depth`, `gc_free_block_threshold`, `read_latency_us`, `program_latency_us`, `erase_latency_us`, `trace_inter_arrival_us`

`ssd_config_validate` 拒絕：

- 任一幾何為 0
- `physical_pages = total_blocks * pages_per_block` 為 0 或 > UINT32_MAX
- `gc_free_block_threshold == 0` 或 `>= total_blocks`
- `logical_pages > physical_pages`

全域 `g_config`（`config.c`）在 `main` 載入後指派給 `g_ftl.config`（透過 `ftl_init` 指標）。

### 8.4 `request_t` / `request_queue_t` 生命週期

#### # Direct Observation

| 欄位 | 設定時機 |
|------|----------|
| `submit_timestamp_us` | `nvme_request_from_submission`（來自 SQ entry） |
| `dispatch_timestamp_us` | `scheduler_run` 開頭，取 `ftl->current_time_us`（若落後則先對齊 submit 時間） |
| `queue_latency_us` | `dispatch - submit` |
| `service_latency_us` | `complete - dispatch` |
| `total_latency_us` | `complete - submit` |

`request_queue_*` 為環形佇列：`head/tail/size/capacity`；滿則 `enqueue` 回 false，使 `nvme_issue_pending` 停止發行。

### 8.5 `nvme.c`：間接呼叫與計數器

#### # Direct Observation

| 成員 | 遞增時機 |
|------|----------|
| `submission_count` | `nvme_submit_write` 成功 |
| `dispatch_count` | 每筆成功 `request_queue_enqueue` |
| `completion_count` | `nvme_post_completion` |
| `reaped_completion_count` | `nvme_reap_completions` 每 reap 一筆 |

`nvme_issue_pending` 跳過 `opcode != NVME_OPCODE_WRITE` 的 SQ entry（僅遞增 sq_head/count，不進 RQ）。

`nvme_post_completion`：CQ 滿回 false；`cq_tail` 回繞時 `cq_phase ^= 1`。

`nvme_has_pending`：SQ、RQ、CQ 任一非空即 true。

#### # Conservative Inference

- Host 端 phase 驗證未實作；reap 僅消耗 CQ slot，適合「模擬 host 已處理完成」的簡化模型。

### 8.6 `block_manager.c` 與 GC 的實際耦合

#### # Direct Observation

- `free_block_pool_push/pop`：`gc_run` 結尾將 victim block 推回 pool；`ftl_init` 將所有 block 先入 pool 再 pop 一個作 `current_write_block`。
- `free_block_pool_get_min_erase_block`：遍歷 pool 內 block，比較 `erase_count[]`，**目前程式碼中未觀察到任何呼叫點**。
- GC victim：`gc_select_victim_block` 使用 **最大 `invalid_page_count`**（跳過 `current_write_block`）。

### 8.7 `nand.c`：`nand_allocate_page` 換 block 條件

#### # Direct Observation

當 `block->write_pointer >= pages_per_block` 時，先 `free_block_pool_pop` 取得新 `current_write_block`，再在新 block 上分配 page。若 pop 失敗或 `free_page_count == 0`，回傳 false。

`nand_program_page` 更新 `valid_page_count++`、`free_page_count--`；`nand_invalidate_page` 在非 VALID 狀態直接 return。

### 8.8 `tests/test_suite.c` 覆蓋範圍（Regression）

#### # Direct Observation

| 測試函式 | 驗證重點 |
|----------|----------|
| `test_config_rejects_impossible_geometry` | `logical_pages` 超出實體容量時 validate 失敗 |
| `test_config_load_rejects_malformed_entries` | 非數值、未知 key |
| `test_nvme_sq_cq_lifecycle` | SQ 深度 2 時第三筆 submit 失敗；issue/post/reap |
| `test_scheduler_pipeline_posts_completions` | 兩筆 write 後 stats 與 nvme 計數一致 |
| `test_ftl_rejects_out_of_range_write` | LBA 越界 `ftl_handle_request` 回 false |
| `assert_latency_accounting_consistent` | queue + service == total |

測試使用 `init_small_config` 縮小 geometry，並可 `disable_logs` 壓低 `g_log_level`。

### 8.9 Callback / 函式指標 / ISR

#### # Direct Observation

- **未觀察到** function pointer 驅動的 dispatch table、callback 註冊、ISR、DMA completion。
- 執行模型為 **同步函式呼叫鏈**（單執行緒）。

### 8.10 錯誤傳播路徑（Error Propagation）

```text
ftl_handle_write 回 false
  → ftl_handle_request 回 false
  → scheduler_run 呼叫 nvme_post_completion(INTERNAL_ERROR)
  → 若 CQ 滿 → nvme_post_completion 回 false → scheduler_run 回 false
  → service_nvme_pipeline 回 false
  → replay_trace / main 設定 rc=1
```

`nand_allocate_page` 失敗路徑：先 `gc_run(true)`，再重試 allocate；仍失敗則 `LOG_ERROR("Out of NAND space")` 並回 false。

### 8.11 Debug / Risk（補充條目）

| 風險 | 依據 | 緩解思路（非現有實作） |
|------|------|------------------------|
| Wear 不均 | GC 未使用 `erase_count` 選 victim | 在 `gc_select_victim_block` 整合 `get_min_erase_block` 或加權 |
| CQ phase 未驗證 | `nvme_reap_completions` 無 phase 檢查 | 模擬 host polling 語意時補上 |
| 全域 `g_ftl` | 單實例 | 多實例測試需改為 context 指標傳遞 |
| Trace 僅 WRITE | `replay_trace` 過濾 | 擴充 opcode 與 FTL read path |

### 8.12 與其他子專題的邊界（避免 overclaim）

#### # Direct Observation

- `ssd-fw-sim` **不包含** Linux kernel module、QEMU、pthread。
- 與 `linux-ipc-benchmark` 的 ring buffer 類似處：SQ/CQ/RQ 皆為 in-process 陣列 + 索引，**無** `copy_from_user` / `mmap`。

---
