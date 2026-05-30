# SSD Firmware Simulator API 與執行語意報告

## 1. 這份文件的閱讀方式

本文件專門說明 `ssd-fw-sim` 的 API 分層、資料流、模組邊界與選擇依據。閱讀時可以把 API 分成三類：

- **入口 API (Entry API)**：外部流程會直接呼叫，例如 `nvme_submit_write()`、`scheduler_run()`、`ftl_handle_request()`。
- **狀態 API (State API)**：修改內部狀態，例如 `mapping_set_physical_page()`、`nand_program_page()`。
- **查詢 API (Query API)**：不改狀態，只回傳目前狀態，例如 `nvme_sq_is_full()`、`gc_needed()`。

整體原則是：上層不要直接改下層資料結構，應透過對應 API 操作。例如 FTL 不應直接改 `nand_page_t.state`，而是呼叫 `nand_program_page()` 或 `nand_invalidate_page()`。

### 1.1 關鍵字速查

| 關鍵字 | 英文 | 在本專案中的意思 |
|--------|------|------------------|
| API | Application Programming Interface | 模組對外提供的函式介面，例如 `nvme_submit_write()` |
| 呼叫者 | Caller | 呼叫某個 API 的上層程式，例如 `main.c` 呼叫 `nvme_submit_write()` |
| 被呼叫者 | Callee | 被呼叫的函式或模組，例如 `scheduler_run()` 呼叫 `ftl_handle_request()` |
| 生命週期 | Lifecycle | 資源從 init、使用、destroy 的完整過程 |
| 所有權 | Ownership | 哪個模組負責配置與釋放資源 |
| 狀態 | State | 結構內記錄目前進度的欄位，例如 `sq_head`、`write_pointer` |
| 前置條件 | Precondition | 呼叫 API 前必須成立的條件，例如 queue 不能是 `NULL`，capacity 不能是 0 |
| 後置條件 | Postcondition | API 成功後保證成立的結果，例如 enqueue 成功後 `size++` |
| 不變量 | Invariant | 程式執行過程中應一直成立的規則，例如 `queue->size <= queue->capacity` |
| 副作用 | Side Effect | API 除了回傳值以外造成的狀態改變，例如 `nand_program_page()` 會改 page state |
| 查詢函式 | Query Function | 只讀取狀態、不修改資料的函式，例如 `request_queue_is_empty()` |
| 狀態變更函式 | Mutating Function | 會修改狀態的函式，例如 `request_queue_enqueue()` |
| 環形佇列 | Ring Buffer / Circular Queue | 使用 head/tail 在固定陣列中循環存取的 queue |
| SQ | Submission Queue | Host 放入命令的 NVMe 佇列 |
| CQ | Completion Queue | Controller 放入完成結果的 NVMe 佇列 |
| RQ | Request Queue | 韌體內部等待 scheduler 處理的請求佇列 |
| LBA | Logical Block Address | Host 指定的邏輯起始位址，本專案簡化成 page 單位 |
| LPN | Logical Page Number | FTL 內部使用的邏輯頁編號 |
| PPA | Physical Page Address | NAND 物理頁位址，由 block index 與 page index 組成 |
| L2P | Logical-to-Physical Mapping | LPN 到 PPA 的對照表 |
| OOB | Out-of-Band / Spare Area | NAND page 額外 metadata 區；本專案用 `logical_page_number` 簡化模擬 |
| GC | Garbage Collection | 搬移有效頁、擦除 block、回收空間的流程 |
| Victim Block | Victim Block | GC 選中準備回收的 block |
| Migration | Migration | GC 將 valid page 搬到新 PPA 的動作 |
| WA | Write Amplification | NAND 實際寫入頁數 / Host 要求寫入頁數 |
| Foreground GC | Foreground Garbage Collection | allocate 失敗後被迫執行，會直接阻塞目前 request |
| Background GC | Background Garbage Collection | 空間低於門檻時預先執行；本專案仍是同步呼叫 |

### 1.2 API 命名規則

本專案的 API 命名大多採用 `模組_動作_對象()`：

| 命名型態 | 例子 | 意義 |
|----------|------|------|
| `*_init()` | `ftl_init()`、`nand_init()` | 建立或初始化資源 |
| `*_destroy()` | `request_queue_destroy()` | 釋放資源並重設欄位 |
| `*_enqueue()` / `*_dequeue()` | `request_queue_enqueue()` | 對 queue 放入或取出元素 |
| `*_is_full()` / `*_is_empty()` | `nvme_sq_is_full()` | 查詢狀態，不應改變資料 |
| `*_get_*()` | `mapping_get_physical_page()` | 查詢資料，通常用回傳值表示是否查到 |
| `*_set_*()` | `mapping_set_physical_page()` | 寫入或更新狀態 |
| `*_run()` | `scheduler_run()`、`gc_run()` | 執行一段完整流程，通常會有多個副作用 |

選 API 時先問三件事：

1. 這個動作是初始化、查詢、狀態變更，還是執行流程？
2. 這個資料的所有權在哪個模組？
3. 呼叫後是否需要同步更新其他 metadata？

例如「把 page 標成有效」不能直接改 `page->state`，因為 valid/free counter 也要一起更新，所以應使用 `nand_program_page()`。

## 2. 模組關係圖

```text
main.c
  |
  +-- config.h / config.c
  |
  +-- nvme.h / nvme.c
  |     |
  |     +-- request.h / request.c
  |
  +-- scheduler.h / scheduler.c
        |
        +-- ftl.h / ftl.c
              |
              +-- mapping.h / mapping.c
              +-- nand.h / nand.c
              |     |
              |     +-- block_manager.h / block_manager.c
              |
              +-- gc.h / gc.c
              +-- stats.h / stats.c
```

資料方向：

```text
Trace
  -> NVMe SQ
  -> Request Queue
  -> Scheduler
  -> FTL
  -> NAND / Mapping / GC
  -> NVMe CQ
  -> Statistics
```

### 2.1 模組邊界與所有權

「所有權 (Ownership)」是讀 C 專案時很重要的觀念。誰配置資源，通常也應由誰釋放；誰維護狀態，也應由誰提供更新 API。

| 模組 | 擁有的資料 | 對外提供的主要 API | 其他模組應避免 |
|------|------------|--------------------|----------------|
| `config.c` | `ssd_config_t` 的預設值與驗證規則 | `ssd_config_init_default()`、`ssd_config_validate()` | 繞過 validate 直接進入模擬 |
| `request.c` | `request_queue_t.entries` | `request_queue_enqueue()`、`request_queue_dequeue()` | 直接改 `head/tail/size` |
| `nvme.c` | SQ/CQ entries、head/tail、phase | `nvme_submit_write()`、`nvme_post_completion()` | 直接寫 SQ/CQ 陣列 |
| `ftl.c` | `g_ftl`、mapping table、free block pool | `ftl_init()`、`ftl_handle_request()` | 從外部直接改 `g_ftl.mapping_table` |
| `mapping.c` | `mapping_entry_t` 的 valid bit 與 PPA | `mapping_get_physical_page()`、`mapping_set_physical_page()` | 直接改 `table[lpn]` |
| `nand.c` | block/page 狀態與 counter | `nand_program_page()`、`nand_erase_block()` | 直接改 `page->state` |
| `gc.c` | GC victim selection 與 migration 流程 | `gc_needed()`、`gc_run()` | 從外部直接 erase victim |
| `stats.c` | 統計欄位的計算規則 | `stats_update_request()`、`stats_print()` | 手動重複計算平均值 |

如果不知道該呼叫哪個 API，可以先找「資料欄位在哪個 struct」，再看該 struct 是哪個模組定義的。舉例來說，`nand_page_t.state` 定義在 `nand.h`，所以狀態改變應從 `nand.c` 的 API 進入。

### 2.2 從 trace 到 completion 的 API 呼叫圖

```text
replay_trace()
  |
  |  讀到 "WRITE 0 4"
  v
nvme_submit_write()
  |
  |  寫入 nvme_controller_t.sq_entries
  v
service_nvme_pipeline()
  |
  +--> nvme_issue_pending()
  |       |
  |       +--> request_queue_enqueue()
  |
  +--> scheduler_run()
  |       |
  |       +--> request_queue_dequeue()
  |       +--> gc_needed()
  |       +--> ftl_handle_request()
  |       |       |
  |       |       +--> mapping_get_physical_page()
  |       |       +--> nand_allocate_page()
  |       |       +--> nand_program_page()
  |       |       +--> nand_invalidate_page()
  |       |       +--> mapping_set_physical_page()
  |       |
  |       +--> nvme_post_completion()
  |       +--> stats_update_request()
  |
  +--> nvme_reap_completions()
```

這張圖可以當成讀 code 的順序。若要 debug 某筆寫入為什麼失敗，就沿著這條呼叫鏈往下看；若要 debug 統計數字，就從 `scheduler_run()` 與 `stats_update_request()` 開始看。

## 3. 初始化與釋放流程

### 3.1 初始化順序

```text
ssd_config_init_default()
  |
  +--> ssd_config_load_file()    可選
  |
  +--> ssd_config_validate()
  |
request_queue_init()
  |
nvme_controller_init()
  |
ftl_init()
  |
  +--> nand_init()
  +--> mapping_table_init()
  +--> free_block_pool_init()
  +--> stats_init()
```

`ftl_init()` 內部會配置 NAND、mapping table、LPN write count 與 free block pool。因此釋放時也由 `ftl_destroy()` 負責這些資源。

### 3.2 釋放順序

```text
ftl_destroy()
  |
  +--> free_block_pool_destroy()
  +--> free(mapping_table)
  +--> free(lpn_write_count)
  +--> nand_destroy()

nvme_controller_destroy()
request_queue_destroy()
```

### 3.3 API 選擇依據

| 想做的事 | 應使用 API | 不建議做法 | 原因 |
|----------|------------|------------|------|
| 初始化預設設定 | `ssd_config_init_default()` | 手動填每個欄位 | 避免漏填欄位 |
| 檢查設定是否合法 | `ssd_config_validate()` | 等執行到 FTL 才處理 | 錯誤越早發現越好 |
| 建立 NVMe 佇列 | `nvme_controller_init()` | 直接 `calloc` SQ/CQ | init API 會設定 capacity 與 phase |
| 建立 FTL | `ftl_init()` | 分別手動呼叫 NAND/mapping init | FTL 擁有這些資源，應集中管理 |
| 釋放 FTL | `ftl_destroy()` | 只釋放 mapping table | 會漏掉 NAND pages 與 free block pool |

## 4. Config API

### 4.1 API 清單

| API | 類型 | 用途 |
|-----|------|------|
| `ssd_config_init_default()` | 初始化 | 填入預設 SSD 幾何與延遲 |
| `ssd_config_load_file()` | 載入 | 從 `key=value` 設定檔覆寫預設值 |
| `ssd_config_validate()` | 驗證 | 檢查設定是否合理 |
| `ssd_config_print()` | 輸出 | 印出目前設定 |

### 4.2 `load` 與 `validate` 的差異

`ssd_config_load_file()` 檢查的是「設定檔能不能被正確解析」：

- 每行是否有 `=`。
- value 是否為純數字。
- key 是否是已知 key。

`ssd_config_validate()` 檢查的是「解析後的設定是否能用」：

- `total_blocks` 不可為 0。
- `logical_pages` 不可大於物理 page 數。
- `gc_free_block_threshold` 不可為 0，也不可大於等於 `total_blocks`。

兩者要分開，因為格式正確不代表語意正確。

範例：

```text
program_latency_us=200
```

這行格式正確，也通常語意正確。

```text
program_latency_us=200us
```

這行語意看起來像 200 微秒，但程式會拒絕，因為 value 必須是純數字。這樣可以避免使用者以為設定成功，實際卻不是預期值。

### 4.3 與類似 API 的比較

| API | 改變資料嗎 | 適合時機 |
|-----|------------|----------|
| `ssd_config_init_default()` | 會 | 程式一開始建立設定 |
| `ssd_config_load_file()` | 會 | 使用者指定 `--config` 後 |
| `ssd_config_validate()` | 不應改變資料 | init/load 後、進入模擬前 |
| `ssd_config_print()` | 不改資料 | 給使用者確認目前設定 |

### 4.4 設定 key 關鍵字說明

| Key | 英文概念 | 說明 | 調大會怎樣 |
|-----|----------|------|------------|
| `total_blocks` | Physical Blocks | NAND block 總數 | 實體容量增加，GC 較晚發生 |
| `pages_per_block` | Pages per Block | 每個 block 的 page 數 | 單次 erase 可回收的 page 變多 |
| `logical_pages` | Logical Capacity | Host 可使用的邏輯 page 數 | 可寫範圍變大，但不可超過實體 page |
| `request_queue_depth` | Queue Depth | SQ、CQ、RQ 的共同深度 | 可暫存更多 request，但會用更多記憶體 |
| `gc_free_block_threshold` | GC Threshold | free block 低於此值就觸發 GC | 越大越早 GC，較不容易到 foreground GC |
| `read_latency_us` | Read Latency | NAND read 模擬延遲 | GC 搬移 valid page 會更慢 |
| `program_latency_us` | Program Latency | NAND page program 模擬延遲 | 每次 host write 與 GC migration 都更慢 |
| `erase_latency_us` | Erase Latency | Block erase 模擬延遲 | GC 尖峰延遲更明顯 |
| `trace_inter_arrival_us` | Inter-arrival Time | trace request 之間的抵達間隔 | 越大代表 host 送 request 越慢 |

### 4.5 Config API 使用範例

```text
ssd_config_t config;

ssd_config_init_default(&config);
ssd_config_load_file("ssd.conf", &config);   若使用者有指定設定檔
ssd_config_validate(&config);
ssd_config_print(&config);
```

這個順序的重點是：先有預設值，再用檔案覆寫，最後檢查整體是否合理。不要先 validate 再 load，因為 load 後的值才是實際要跑的設定。

## 5. Request Queue API

### 5.1 資料結構

`request_queue_t` 是韌體內部的環形佇列：

```text
entries[capacity]
head -> 下一個 dequeue 位置
tail -> 下一個 enqueue 位置
size -> 目前元素數量
```

### 5.2 API 清單

| API | 用途 |
|-----|------|
| `request_queue_init()` | 配置 queue entries |
| `request_queue_destroy()` | 釋放 entries |
| `request_queue_enqueue()` | 放入 request |
| `request_queue_dequeue()` | 取出 request |
| `request_queue_is_full()` | 查詢是否已滿 |
| `request_queue_is_empty()` | 查詢是否為空 |

### 5.3 Ring Buffer 運作圖

```text
capacity = 4

初始:
[ _ ][ _ ][ _ ][ _ ]
  H
  T

enqueue A, B:
[ A ][ B ][ _ ][ _ ]
  H         T

dequeue A:
[ _ ][ B ][ _ ][ _ ]
      H     T

enqueue C, D, E:
[ E ][ B ][ C ][ D ]
      H
      T
```

tail 或 head 到陣列尾端時，會用 `% capacity` 回到 0。

### 5.4 與 NVMe Queue 的差異

| 項目 | Request Queue | NVMe SQ/CQ |
|------|---------------|------------|
| 所屬層級 | 韌體內部 | Host/controller 介面 |
| 元素型別 | `request_t` | `nvme_submission_entry_t` / `nvme_completion_entry_t` |
| 主要用途 | 等待 scheduler 處理 | 模擬 NVMe submit/complete |
| command id | 從 NVMe entry 帶入 | 由 NVMe submit 產生 |

如果要模擬 host 發出 write，應呼叫 `nvme_submit_write()`，不要直接 `request_queue_enqueue()`。直接塞 request 會跳過 NVMe SQ 的 command id 與 submission count。

### 5.5 `request_t` 欄位說明

| 欄位 | 英文概念 | 設定者 | 說明 |
|------|----------|--------|------|
| `type` | Request Type | `nvme_request_from_submission()` | 目前固定為 `REQUEST_TYPE_WRITE` |
| `command_id` | Command Identifier | `nvme_submit_write()` | 追蹤 request 與 completion 的對應關係 |
| `queue_id` | Queue Identifier | `nvme_request_from_submission()` | 本專案固定為 1 |
| `lba` | Start LBA | trace / SQ entry | 寫入起始邏輯位址 |
| `length` | Page Count | trace / SQ entry | 要寫入幾個 page |
| `submit_timestamp_us` | Submit Time | `nvme_submit_write()` | host submit 的模擬時間 |
| `dispatch_timestamp_us` | Dispatch Time | `scheduler_run()` | scheduler 開始處理的時間 |
| `complete_timestamp_us` | Complete Time | `scheduler_run()` | request 完成時間 |
| `queue_latency_us` | Queue Latency | `scheduler_run()` | dispatch - submit |
| `service_latency_us` | Service Latency | `scheduler_run()` | complete - dispatch |
| `total_latency_us` | Total Latency | `scheduler_run()` | complete - submit |

### 5.6 `is_full()` 與 `enqueue()` 的選擇

| 情境 | 建議 API | 原因 |
|------|----------|------|
| 只是想檢查 queue 是否可放資料 | `request_queue_is_full()` | 不改變 queue 狀態 |
| 確定要放入 request | `request_queue_enqueue()` | 會寫入 entry、移動 tail、增加 size |
| 只是想檢查 queue 是否有資料 | `request_queue_is_empty()` | 不改變 queue 狀態 |
| 確定要取出 request | `request_queue_dequeue()` | 會複製 entry、移動 head、減少 size |

`enqueue()` 內部已經會檢查 full，所以呼叫者不一定要先呼叫 `is_full()`。但若呼叫者想在滿佇列時先做其他事，例如先 drain pipeline，就可以先用 `is_full()` 判斷。

## 6. NVMe API

### 6.1 API 清單

| API | 類型 | 用途 |
|-----|------|------|
| `nvme_controller_init()` | 初始化 | 配置 SQ/CQ |
| `nvme_controller_destroy()` | 釋放 | 釋放 SQ/CQ |
| `nvme_submit_write()` | 入口 | Host 提交 write command |
| `nvme_issue_pending()` | 搬移 | SQ entry 轉成 internal request |
| `nvme_post_completion()` | 完成 | Controller 放入 CQ |
| `nvme_reap_completions()` | 完成處理 | Host 取走 CQ entry |
| `nvme_has_pending()` | 查詢 | SQ/RQ/CQ 是否仍有未完成工作 |
| `nvme_sq_is_full()` | 查詢 | SQ 是否滿 |
| `nvme_cq_is_full()` | 查詢 | CQ 是否滿 |

### 6.2 寫入命令生命週期

```text
nvme_submit_write()
  |
  |  nvme_submission_entry_t
  v
Submission Queue
  |
  |  nvme_issue_pending()
  v
request_t
  |
  |  scheduler_run()
  v
FTL / NAND
  |
  |  nvme_post_completion()
  v
Completion Queue
  |
  |  nvme_reap_completions()
  v
Done
```

### 6.3 `submit`、`issue`、`post`、`reap` 的差異

| API | 誰的動作 | 位置 | 意義 |
|-----|----------|------|------|
| `nvme_submit_write()` | Host | SQ tail | 主機提交新指令 |
| `nvme_issue_pending()` | Controller | SQ head -> RQ tail | 控制器取出待處理指令 |
| `nvme_post_completion()` | Controller | CQ tail | 控制器回報指令完成 |
| `nvme_reap_completions()` | Host | CQ head | 主機取走完成項 |

這四個 API 分開後，可以清楚描述 NVMe 的非同步佇列語意。即使模擬器本身是同步跑完，資料結構仍保留「host submit」與「controller complete」之間的距離。

### 6.4 Phase Bit

`nvme_post_completion()` 會在 CQ tail 回到 0 時翻轉 `cq_phase`：

```text
CQ capacity = 4

tail: 0 -> 1 -> 2 -> 3 -> 0
                         |
                         v
                     phase toggle
```

真實 NVMe host 會利用 phase bit 判斷 CQ entry 是新的還是舊的。本專案目前只在 completion entry 中寫入 phase，`nvme_reap_completions()` 沒有做 host 端 phase 驗證。

### 6.5 使用情境比較

| 情境 | 使用 API | 原因 |
|------|----------|------|
| trace replay 讀到 `WRITE 0 4` | `nvme_submit_write()` | 保留 host submit 語意 |
| SQ 有資料且 RQ 還有空間 | `nvme_issue_pending()` | 把協定層指令轉成韌體 request |
| FTL 處理成功 | `nvme_post_completion(..., NVME_STATUS_SUCCESS, ...)` | 回報成功 |
| FTL 處理失敗 | `nvme_post_completion(..., NVME_STATUS_INTERNAL_ERROR, ...)` | 回報錯誤 |
| 主機輪詢完成項 | `nvme_reap_completions()` | 釋放 CQ slot |

### 6.6 NVMe 結構欄位說明

`nvme_submission_entry_t` 代表 host 放進 SQ 的命令：

| 欄位 | 英文概念 | 說明 |
|------|----------|------|
| `command_id` | Command Identifier | 每筆命令的識別碼 |
| `slba` | Starting LBA | 起始邏輯位址 |
| `nlb` | Number of Logical Blocks | 寫入長度；本專案簡化為 page 數 |
| `opcode` | Operation Code | 目前只支援 `NVME_OPCODE_WRITE` |
| `submit_timestamp_us` | Submit Timestamp | host 提交時間 |

`nvme_completion_entry_t` 代表 controller 放進 CQ 的結果：

| 欄位 | 英文概念 | 說明 |
|------|----------|------|
| `command_id` | Command Identifier | 對應原本的 SQ command |
| `sq_head` | SQ Head Pointer | completion 產生時的 SQ head |
| `sq_id` | SQ Identifier | 本專案固定使用 queue id 1 |
| `status` | Status Code | 成功或錯誤碼 |
| `phase` | Phase Bit | 區分 CQ entry 新舊輪次 |
| `complete_timestamp_us` | Complete Timestamp | request 完成時間 |

### 6.7 NVMe API 選擇決策圖

```text
想處理 NVMe 佇列？
  |
  +-- Host 要提交新 write
  |     |
  |     +--> nvme_submit_write()
  |
  +-- Controller 要把 SQ 命令交給韌體
  |     |
  |     +--> nvme_issue_pending()
  |
  +-- FTL / scheduler 已處理完 request
  |     |
  |     +--> nvme_post_completion()
  |
  +-- Host 要消耗完成項、釋放 CQ slot
  |     |
  |     +--> nvme_reap_completions()
  |
  +-- main 想知道整條 pipeline 是否還有事
        |
        +--> nvme_has_pending()
```

### 6.8 SQ 滿與 CQ 滿的處理差異

| 滿的地方 | API 會怎麼反應 | 合理處理方式 |
|----------|----------------|--------------|
| SQ 滿 | `nvme_submit_write()` 回傳 `-1` | 先呼叫 pipeline 消耗 SQ，再重試 submit |
| Request Queue 滿 | `nvme_issue_pending()` 停止 issue | 讓 scheduler 消耗 request queue |
| CQ 滿 | `nvme_post_completion()` 回傳 `false` | 讓 host reap completion，釋放 CQ slot |

SQ 滿代表 host 提交太快；RQ 滿代表韌體還沒處理完；CQ 滿代表 host 還沒取走完成項。三者都是「佇列滿」，但瓶頸位置不同。

## 7. Scheduler API

### 7.1 API

```c
bool scheduler_run(ftl_context_t *ftl,
                   request_queue_t *queue,
                   nvme_controller_t *controller);
```

`scheduler_run()` 目前是同步 dispatcher。它會持續從 request queue 取 request，直到 queue 為空或處理失敗。

### 7.2 工作內容

```text
request_queue_dequeue()
  |
  +--> 對齊 current_time_us 與 submit_timestamp_us
  |
  +--> 設定 dispatch_timestamp_us
  |
  +--> gc_needed()? gc_run(false)
  |
  +--> ftl_handle_request()
  |
  +--> 設定 complete_timestamp_us
  |
  +--> nvme_post_completion()
  |
  +--> stats_update_request()
```

### 7.3 Scheduler 與 FTL 的分工

| 項目 | Scheduler | FTL |
|------|-----------|-----|
| 決定 request 執行順序 | 是 | 否 |
| 計算 queue/service/total latency | 是 | 否 |
| 處理 LPN 到 PPA | 否 | 是 |
| 分配 NAND page | 否 | 是 |
| 更新 mapping | 否 | 是 |
| post completion | 是 | 否 |

這樣分層的好處是：FTL 不需要知道 NVMe CQ，也不需要負責 host completion。FTL 專心處理儲存語意，scheduler 負責把 request 從佇列推進到完成狀態。

### 7.4 Scheduler 時間線圖

```text
Host submit time
  |
  |  request 在 SQ / RQ 等待
  v
Scheduler dispatch time
  |
  |  FTL write
  |  NAND program
  |  GC if needed
  v
Completion time
```

對應 API 與欄位：

| 時間點 | 設定位置 | 欄位 |
|--------|----------|------|
| Host submit | `nvme_submit_write()` | `submit_timestamp_us` |
| Scheduler dispatch | `scheduler_run()` | `dispatch_timestamp_us` |
| 完成處理 | `scheduler_run()` | `complete_timestamp_us` |

如果 `current_time_us` 小於 `submit_timestamp_us`，scheduler 會先把 device time 推進到 request 抵達時間。這代表裝置在等待下一筆 IO，而不是 request 在排隊。

### 7.5 `scheduler_run()` 何時回傳 false

| 失敗點 | 可能原因 | 後續影響 |
|--------|----------|----------|
| `ftl_handle_request()` 回 false | LBA 越界、NAND 空間不足、GC 失敗 | 會嘗試 post internal error completion |
| `nvme_post_completion()` 回 false | CQ 滿 | scheduler 無法回報完成，pipeline 失敗 |

`scheduler_run()` 本身不直接處理 trace，也不解析 config。它只相信已經進入 request queue 的 request，再把成功或失敗結果轉成 completion。

## 8. FTL API

### 8.1 API 清單

| API | 用途 |
|-----|------|
| `ftl_init()` | 初始化 FTL 全域 context |
| `ftl_destroy()` | 釋放 FTL 擁有的資源 |
| `ftl_handle_request()` | 處理 request，目前只支援 write |

### 8.2 `ftl_context_t`

`ftl_context_t` 是 FTL 的主要狀態集合：

| 欄位 | 意義 |
|------|------|
| `config` | 指向 SSD 設定 |
| `nand` | NAND 模擬器 |
| `mapping_table` | L2P table |
| `lpn_write_count` | 每個 LPN 被寫入次數 |
| `free_block_pool` | 尚未使用或 GC 後可重用的 block |
| `current_write_block` | 目前正在寫入的 block |
| `current_time_us` | 模擬時間 |
| `last_write_end_lpn` | 判斷 sequential write 用 |
| `stats` | 統計資料 |

### 8.3 `ftl_handle_request()` 與 `ftl_handle_write()`

`ftl_handle_request()` 是公開入口，負責根據 request type 分派。目前只有：

```text
REQUEST_TYPE_WRITE -> ftl_handle_write()
```

`ftl_handle_write()` 是檔案內的 static helper，負責真正寫入每個 LPN。

### 8.4 寫入一個 LPN 的 API 順序

```text
mapping_get_physical_page()
  |
  |  若已有舊 PPA，先記住
  v
gc_needed()? gc_run(false)
  |
  v
nand_allocate_page()
  |
  |  若失敗，gc_run(true) 後重試
  v
nand_program_page()
  |
  v
nand_invalidate_page(old_ppa)   若有舊 PPA
  |
  v
mapping_set_physical_page()
  |
  v
更新 stats
```

### 8.5 為什麼要先 program 再 invalidate 舊頁

如果先把舊頁 invalid，再寫新頁失敗，LPN 會暫時沒有有效資料。雖然本專案沒有做斷電恢復，但這個順序仍比較合理：

```text
較安全的順序:
新頁寫入成功 -> 舊頁 invalid -> mapping 指向新頁

風險較高的順序:
舊頁 invalid -> 新頁寫入失敗 -> mapping 無有效資料可指
```

### 8.6 LBA、LPN、PPA 的關係圖

```text
Host command:
WRITE LBA=100 LENGTH=4
  |
  v
FTL 拆成 LPN:
100, 101, 102, 103
  |
  v
每個 LPN 查 L2P:
LPN 100 -> 舊 PPA? 可能有，也可能沒有
  |
  v
分配新 PPA:
PPA(block_index, page_index)
  |
  v
更新 L2P:
LPN 100 -> new PPA
```

本專案把 LBA 與 LPN 簡化成同一個 page 單位；真實 SSD 常會有 sector 到 page 的換算，例如 4KB page 包含多個 512B sector。

### 8.7 FTL API 選擇表

| 想做的事 | 使用 API / 函式 | 不應直接做的事 |
|----------|-----------------|----------------|
| 建立 FTL context | `ftl_init()` | 直接配置 `g_ftl.mapping_table` |
| 結束模擬 | `ftl_destroy()` | 只釋放部分欄位 |
| 處理外部 request | `ftl_handle_request()` | 從 scheduler 直接呼叫 `ftl_handle_write()`，因為它是 static helper |
| 查舊 PPA | `mapping_get_physical_page()` | 直接讀 `mapping_table[lpn]` |
| 寫新 page | `nand_allocate_page()` + `nand_program_page()` | 直接改 NAND page state |
| 更新 L2P | `mapping_set_physical_page()` | 手動改 `valid` 與 `ppa` 欄位 |

### 8.8 FTL 正確性的三個不變量

| 不變量 | 說明 |
|--------|------|
| 有效 LPN 的 mapping 必須指向 valid page | `mapping_get_physical_page()` 找到的 PPA，其 page state 應是 `NAND_PAGE_VALID` |
| 同一個 LPN 的舊 page 需被 invalid | out-of-place update 後，舊 PPA 不可仍被視為最新資料 |
| GC 搬移 valid page 後必須更新 mapping | 搬移改變 PPA，L2P 必須同步 |

如果日後加入 read path，第一個不變量會直接影響讀取正確性。

## 9. Mapping API

### 9.1 API 清單

| API | 用途 |
|-----|------|
| `mapping_table_init()` | 清空 mapping table |
| `mapping_get_physical_page()` | 查 LPN 是否已有 PPA |
| `mapping_set_physical_page()` | 設定 LPN -> PPA |
| `mapping_clear()` | 清除某個 LPN 的 mapping |

### 9.2 `get`、`set`、`clear` 比較

| API | 改變 table | 回傳值 | 使用時機 |
|-----|------------|--------|----------|
| `mapping_get_physical_page()` | 否 | `true/false` | 寫入前查是否有舊 PPA |
| `mapping_set_physical_page()` | 是 | 無 | 新 page program 成功後 |
| `mapping_clear()` | 是 | 無 | 目前較少用，可用於未來 trim/unmap |

### 9.3 常見錯誤

不要在 `nand_program_page()` 前就更新 mapping。若 NAND program 失敗，mapping 會指向一個不可靠的位置。

本專案目前 `nand_program_page()` 沒有回傳失敗，但 API 順序仍保留合理語意，方便未來加入 program failure 模擬。

### 9.4 Mapping API 選擇依據

```text
要處理 LPN mapping？
  |
  +-- 只是想知道 LPN 有沒有位置
  |     |
  |     +--> mapping_get_physical_page()
  |
  +-- 新 page 已寫好，要讓 LPN 指到新 PPA
  |     |
  |     +--> mapping_set_physical_page()
  |
  +-- 未來支援 TRIM / UNMAP，要取消 LPN
        |
        +--> mapping_clear()
```

### 9.5 `valid` bit 的意義

`mapping_entry_t.valid` 代表「這個 LPN 是否已有對應的 PPA」。它不是 NAND page 的 valid state，兩者不要混在一起：

| 欄位 | 所屬結構 | 意義 |
|------|----------|------|
| `mapping_entry_t.valid` | L2P table | LPN 是否有 mapping |
| `nand_page_t.state == NAND_PAGE_VALID` | NAND page | 這個 physical page 是否保存最新資料 |

正常情況下，若 `mapping_entry_t.valid == true`，它指到的 NAND page 也應該是 `NAND_PAGE_VALID`。這就是 GC 測試要驗證的事。

## 10. NAND API

### 10.1 API 清單

| API | 類型 | 用途 |
|-----|------|------|
| `nand_init()` | 初始化 | 建立 block/page 陣列 |
| `nand_destroy()` | 釋放 | 釋放所有 pages |
| `nand_allocate_page()` | 分配 | 找下一個可 program 的 PPA |
| `nand_program_page()` | 狀態變更 | page: FREE -> VALID |
| `nand_invalidate_page()` | 狀態變更 | page: VALID -> INVALID |
| `nand_erase_block()` | 狀態變更 | 整個 block reset 成 FREE |
| `nand_is_block_free()` | 查詢 | 判斷 block 是否全空 |
| `nand_get_free_block_count()` | 查詢 | 掃描 NAND 計算 free block 數 |

### 10.2 `allocate` 與 `program` 的差異

這是 NAND API 裡最容易混淆的一組。

| API | 做什麼 | 不做什麼 |
|-----|--------|----------|
| `nand_allocate_page()` | 回傳下一個 PPA，移動 write pointer | 不改 page state，不增加 valid count |
| `nand_program_page()` | 將指定 PPA 改成 VALID，記錄 LPN | 不選 PPA |

為什麼要拆開？因為 FTL 需要先知道新 PPA，才可以決定後續 mapping 更新與錯誤處理。若 allocate 與 program 綁在一起，會比較難表達「已取得位置，但 program 還沒完成」這個階段。

### 10.3 狀態變化圖

```text
nand_allocate_page()
  |
  |  只決定 PPA，不改 state
  v
PPA(block, page)
  |
  |  nand_program_page()
  v
NAND_PAGE_VALID
  |
  |  nand_invalidate_page()
  v
NAND_PAGE_INVALID
  |
  |  nand_erase_block()
  v
NAND_PAGE_FREE
```

### 10.4 `nand_get_free_block_count()` 與 `free_block_pool_count()`

這兩個 API 都像是在問「還有多少空間」，但語意不同。

| API | 來源 | 成本 | 用途 |
|-----|------|------|------|
| `nand_get_free_block_count()` | 掃描 NAND block 狀態 | O(number of blocks) | 檢查物理狀態 |
| `free_block_pool_count()` | 查 free block pool metadata | O(1) | GC trigger 與 allocation |

GC 判斷使用 `free_block_pool_count()`，因為 allocation 也是從 free block pool 取 block。這和實際可被寫入流程使用的資源一致。

### 10.5 NAND 狀態 counter 的同步關係

`nand_program_page()` 與 `nand_invalidate_page()` 不只改 page state，也會更新 block counter：

```text
nand_program_page()
  page: FREE -> VALID
  valid_page_count++
  free_page_count--

nand_invalidate_page()
  page: VALID -> INVALID
  valid_page_count--
  invalid_page_count++

nand_erase_block()
  block reset
  valid_page_count = 0
  invalid_page_count = 0
  free_page_count = pages_per_block
  write_pointer = 0
  erase_count++
```

這就是為什麼不能從 FTL 直接寫 `page->state`。只改 state 會讓 counter 留在舊值，GC victim selection 與 free block 判斷都可能失真。

### 10.6 NAND API 選擇情境

| 情境 | 使用 API | 原因 |
|------|----------|------|
| 需要下一個可寫 PPA | `nand_allocate_page()` | 它會處理 current block 是否已滿 |
| 已取得 PPA，要寫入 LPN | `nand_program_page()` | 它會設定 page metadata 與 counter |
| LPN 被更新，舊 PPA 過期 | `nand_invalidate_page()` | 它會將 valid page 轉成 invalid |
| GC 要回收 victim block | `nand_erase_block()` | 它會 reset block 並增加 erase count |
| 想確認整個 block 是否空 | `nand_is_block_free()` | 它依照 block 內 free page 數判斷 |

### 10.7 Allocate 失敗時代表什麼

`nand_allocate_page()` 回傳 `false` 可能代表：

- 目前 `current_write_block` 已滿。
- free block pool 沒有下一個 block。
- 新 block 也沒有 free page。

FTL 收到失敗後，不會立刻放棄，而是先執行 foreground GC：

```text
nand_allocate_page() failed
  |
  +--> gc_run(foreground = true)
  |
  +--> nand_allocate_page() retry
```

如果重試仍失敗，才回報 out of NAND space。

## 11. Block Manager API

### 11.1 API 清單

| API | 用途 |
|-----|------|
| `free_block_pool_init()` | 建立 free block pool |
| `free_block_pool_destroy()` | 釋放 pool |
| `free_block_pool_push()` | 把可用 block 放回 pool |
| `free_block_pool_pop()` | 取出下一個可用 block |
| `free_block_pool_count()` | 查詢 pool 內 block 數 |
| `free_block_pool_get_min_erase_block()` | 從 pool 找 erase count 最小的 block |

### 11.2 Push / Pop 語意

```text
ftl_init():
  所有 block push 到 pool
  pop 一個成為 current_write_block

nand_allocate_page():
  current_write_block 滿了
  pop 下一個 free block

gc_run():
  victim erase 完
  push 回 pool
```

### 11.3 未使用但有價值的 API

`free_block_pool_get_min_erase_block()` 目前沒有被 GC 呼叫。它可以作為未來 wear leveling 的基礎：當要從 free pool 選新 block 時，可以優先選 erase count 較小的 block，避免部分 block 磨耗過高。

目前沒有使用它，是因為本專案的第一版重點在 write path 與 greedy GC，不是完整磨耗平均。

### 11.4 Free Block Pool 與 NAND free block 的差異

Free block pool 是「FTL 可拿來當下一個 write block 的候選清單」。NAND block 物理上全空，不代表它一定在 pool 裡；例如目前正在寫入的 `current_write_block` 可能還有 free page，但它不在 pool 內。

```text
所有 NAND block
  |
  +-- current_write_block    正在被寫入，不在 free pool
  |
  +-- free_block_pool        尚未使用或 GC erase 後可用
  |
  +-- used blocks            有 valid/invalid page，等待 GC
```

選擇 API 時的判斷：

| 想知道什麼 | 使用 API |
|------------|----------|
| GC trigger 還有多少可切換 block | `free_block_pool_count()` |
| 物理上有幾個完整 free block | `nand_get_free_block_count()` |
| 要拿下一個 write block | `free_block_pool_pop()` |
| GC erase 後歸還 block | `free_block_pool_push()` |

## 12. GC API

### 12.1 API 清單

| API | 用途 |
|-----|------|
| `gc_needed()` | 判斷 free block 是否低於門檻 |
| `gc_run()` | 執行 GC |

檔案內還有 static helper：

| Helper | 用途 |
|--------|------|
| `gc_select_victim_block()` | 選 invalid page 最多的 block |
| `gc_migrate_valid_pages()` | 搬移 victim 內的 valid page |

### 12.2 `gc_needed()` 與 `gc_run()` 的差異

| API | 是否改變狀態 | 成本 | 使用時機 |
|-----|--------------|------|----------|
| `gc_needed()` | 否 | 很低 | 寫入前、scheduler dispatch 前 |
| `gc_run()` | 是 | 可能很高 | 確定需要回收時 |

`gc_needed()` 只是判斷，不能保證 `gc_run()` 一定成功。若沒有任何 block 有 invalid page，GC 可能找不到 victim。

### 12.3 GC 重點流程圖

```text
gc_run()
  |
  v
gc_select_victim_block()
  |
  |  找 invalid_page_count 最大者
  v
gc_migrate_valid_pages()
  |
  +--> nand_allocate_page()
  +--> current_time_us += read_latency_us
  +--> nand_program_page()
  +--> current_time_us += program_latency_us
  +--> mapping_set_physical_page()
  +--> nand_invalidate_page(old_ppa)
  |
  v
nand_erase_block()
  |
  +--> current_time_us += erase_latency_us
  +--> stats.nand_erase_count++
  +--> stats.gc_count++
  |
  v
free_block_pool_push(victim)
```

### 12.4 Greedy Policy 與其他策略比較

| 策略 | 英文 | 選擇依據 | 優點 | 缺點 |
|------|------|----------|------|------|
| 貪婪策略 | Greedy Policy | invalid page 最多 | 簡單、回收空間多 | 不看 block 磨耗 |
| 成本效益 | Cost-Benefit | invalid ratio、age、搬移成本 | 可降低 WA | 需要更多 metadata |
| 磨耗感知 | Wear-aware | erase count | 避免特定 block 過度磨耗 | 可能犧牲短期效能 |

本專案選 greedy，是因為它最容易觀察 GC 對 WA 與 latency 的影響，也能用少量 metadata 完成。

### 12.5 GC 觸發決策圖

```text
寫入前或 scheduler dispatch 前
  |
  v
gc_needed()
  |
  +-- false
  |     |
  |     +--> 繼續 host write
  |
  +-- true
        |
        v
      gc_run(false)   background GC 統計
        |
        +-- 成功：繼續 host write
        |
        +-- 失敗：目前流程多半忽略這次預防性 GC，等 allocate 失敗再處理

nand_allocate_page() 失敗
  |
  v
gc_run(true)          foreground GC 統計
  |
  +-- 成功：重試 allocate
  |
  +-- 失敗：request 失敗
```

### 12.6 `foreground` 參數怎麼選

| 呼叫位置 | `foreground` | 原因 |
|----------|--------------|------|
| `scheduler_run()` 中預先檢查 | `false` | request 還沒 allocate 失敗，是預防性回收 |
| `ftl_handle_write()` 每頁寫入前 | `false` | 長 request 中保留 migration 空間 |
| `nand_allocate_page()` 失敗後 | `true` | 目前 request 已經被空間不足阻塞 |

Foreground GC 與 background GC 在本專案都會同步執行；差別在統計語意。Foreground 表示使用者 request 已經被迫等 GC，background 表示在還沒完全失敗前先做回收。

### 12.7 GC 與 WA 的關係圖

```text
Host write
  |
  +--> nand_write_count += host pages
  |
  v
空間變少，invalid page 變多
  |
  v
GC migration
  |
  +--> nand_read_count += migrated valid pages
  +--> nand_write_count += migrated valid pages
  +--> migrated_page_count += migrated valid pages
  |
  v
WA = nand_write_count / host_page_count
```

GC 搬移越多 valid page，`nand_write_count` 越高，WA 也越高。因此 victim selection 才會偏好 invalid page 多的 block，因為 invalid page 不需要搬移。

## 13. Stats API

### 13.1 API 清單

| API | 用途 |
|-----|------|
| `stats_init()` | 歸零統計 |
| `stats_write_amplification()` | 計算 WA |
| `stats_average_latency_us()` | 計算平均 total latency |
| `stats_update_request()` | 累積 latency 統計 |
| `stats_print()` | 印出統計 |
| `stats_export_csv()` | 輸出 CSV |

### 13.2 `stats_update_request()` 與其他 stats API

`stats_update_request()` 是 request 完成時的累積點：

```text
total_queue_latency_us += queue_latency_us
total_service_latency_us += service_latency_us
total_latency_us += total_latency_us
max_* = max(max_*, current)
```

`stats_write_amplification()` 與 `stats_average_latency_us()` 則是查詢 API。它們根據目前統計值計算結果，不修改 stats。

### 13.3 WA 計算

```text
WA = nand_write_count / host_page_count
```

`nand_write_count` 包含：

- Host write 造成的 NAND program。
- GC 搬移 valid page 造成的 NAND program。

因此只要 GC 搬移越多，WA 就會越高。

### 13.4 Latency 統計

```text
submit_timestamp_us
  |
  | queue latency
  v
dispatch_timestamp_us
  |
  | service latency
  v
complete_timestamp_us

total latency = queue latency + service latency
```

測試會檢查：

```text
total_queue_latency_us + total_service_latency_us == total_latency_us
```

這個檢查可以防止時間欄位定義不一致。

### 13.5 `stats_print()` 與 `stats_export_csv()` 的選擇

| 需求 | 使用 API | 原因 |
|------|----------|------|
| 人在終端機快速看結果 | `stats_print()` | 輸出排版過的統計文字 |
| 想把結果丟到 spreadsheet 或畫圖 | `stats_export_csv()` | CSV 容易被其他工具讀取 |
| 只想在程式內拿 WA | `stats_write_amplification()` | 不需要印出所有欄位 |
| 只想在程式內拿平均延遲 | `stats_average_latency_us()` | 不需要重複寫除法與 0 檢查 |

### 13.6 統計欄位與更新位置

| 欄位 | 主要更新位置 | 說明 |
|------|--------------|------|
| `host_request_count` | `scheduler_run()` | request 成功處理後增加 |
| `host_page_count` | `ftl_handle_write()` | 每寫一個 host page 增加 |
| `nand_write_count` | `ftl_handle_write()`、`gc_migrate_valid_pages()` | host write 與 GC migration 都會增加 |
| `nand_read_count` | `gc_migrate_valid_pages()` | 搬移 valid page 前要讀出 |
| `nand_erase_count` | `gc_run()` | victim block erase 後增加 |
| `gc_count` | `gc_run()` | 每次 GC 成功完成後增加 |
| `migrated_page_count` | `gc_migrate_valid_pages()` | 每搬一個 valid page 增加 |
| `foreground_gc_count` | `gc_run(true)` | 前台 GC 次數 |
| `background_gc_count` | `gc_run(false)` | 背景 GC 次數 |
| `sequential_write_count` | `ftl_handle_write()` | request-level sequential 統計 |
| `random_write_count` | `ftl_handle_write()` | request-level random 統計 |

## 14. Main Pipeline API 串接

`main.c` 中最重要的 helper 是 `service_nvme_pipeline()`：

```text
service_nvme_pipeline()
  |
  +--> nvme_issue_pending()
  |
  +--> scheduler_run()
  |
  +--> nvme_reap_completions()
```

它每次做三件事：

1. 把 SQ 裡的 command 發行到 request queue。
2. 讓 scheduler 處理 request queue。
3. 把 CQ completion 清掉。

`replay_trace()` 每讀到一筆 `WRITE`，就會 submit 一次並 service pipeline。trace 結束後，`main()` 還會用：

```c
while (nvme_has_pending(&nvme, &request_queue)) {
    service_nvme_pipeline(...);
}
```

確保 SQ、RQ、CQ 都清空才印統計。

## 15. 錯誤傳播路徑

### 15.1 FTL 寫入失敗

```text
ftl_handle_write() 回 false
  |
  v
ftl_handle_request() 回 false
  |
  v
scheduler_run()
  |
  +--> nvme_post_completion(INTERNAL_ERROR)
  +--> LOG_ERROR(...)
  +--> return false
  |
  v
service_nvme_pipeline() 回 false
  |
  v
main() 設定 rc = 1
```

### 15.2 NVMe CQ 滿

```text
nvme_post_completion() 發現 CQ full
  |
  v
return false
  |
  v
scheduler_run() 回 false
```

目前 `service_nvme_pipeline()` 每次都會 reap completions，因此一般流程不容易讓 CQ 滿。測試中仍會直接測 SQ/CQ 滿佇列行為。

### 15.3 NAND 空間不足

```text
nand_allocate_page() 失敗
  |
  v
gc_run(true)
  |
  v
nand_allocate_page() 再試一次
  |
  +--> 成功：繼續寫入
  |
  +--> 失敗：LOG_ERROR("Out of NAND space")，回 false
```

這條路徑是 foreground GC，代表 host request 已被迫等待空間回收。

## 16. 重要功能運作圖

### 16.1 Out-of-place Update

```text
原本:
LPN 0 -> PPA(0,0)
PPA(0,0) = VALID

再次寫入 LPN 0:
  |
  v
nand_allocate_page() -> PPA(0,1)
  |
  v
nand_program_page(PPA(0,1), LPN 0)
  |
  v
nand_invalidate_page(PPA(0,0))
  |
  v
mapping_set_physical_page(LPN 0, PPA(0,1))

結果:
PPA(0,0) = INVALID
PPA(0,1) = VALID
LPN 0 -> PPA(0,1)
```

### 16.2 GC 搬移 Valid Page

```text
Victim Block 2
page 0: INVALID
page 1: VALID, LPN 10
page 2: INVALID
page 3: VALID, LPN 11

GC:
  page 1 -> new PPA(4,0), update LPN 10 mapping
  page 3 -> new PPA(4,1), update LPN 11 mapping
  erase block 2
  push block 2 back to free pool
```

### 16.3 Request Latency

```text
submit at 10 us
  |
  | request waits in queue
  v
dispatch at 50 us
  |
  | NAND program / GC / completion
  v
complete at 450 us

Queue Latency   = 50 - 10  = 40 us
Service Latency = 450 - 50 = 400 us
Total Latency   = 450 - 10 = 440 us
```

## 17. 開發過程中的 Bug、原因與修正 API

### 17.1 GC 後 L2P 指到舊 PPA

相關 API：

- `gc_migrate_valid_pages()`
- `nand_allocate_page()`
- `nand_program_page()`
- `mapping_set_physical_page()`
- `nand_invalidate_page()`
- `nand_erase_block()`

問題：

GC 搬移 valid page 後，如果只寫到新 PPA，卻沒有更新 mapping table，LPN 仍會指向 victim block 裡的舊 PPA。victim block erase 後，該 PPA 已不再保存有效資料。

修正：

在 GC migration 完成新 page program 後，立即更新 L2P：

```c
mapping_set_physical_page(ftl->mapping_table,
                          page->logical_page_number,
                          &new_ppa);
```

發生原因：

GC 會改變有效資料的物理位置。只處理 NAND 狀態而忘記更新 FTL metadata，就會造成邏輯位址與物理位置不一致。

回歸測試：

`test_gc_preserves_valid_mappings()` 檢查 GC 後每個 LPN 都能查到 mapping，且 mapping 指向的 page 必須是 `NAND_PAGE_VALID`。

### 17.2 長 request 讓 GC 沒有搬移空間

相關 API：

- `ftl_handle_write()`
- `gc_needed()`
- `gc_run()`
- `nand_allocate_page()`

問題：

如果一筆 request 一次寫很多 page，可能在 loop 中持續消耗 free block。等到 allocate 真的失敗時才做 foreground GC，GC 搬移 valid page 也需要新 page，這時可能已經沒有足夠空間。

修正：

在每個 LPN 寫入前先檢查：

```c
if (gc_needed(ftl)) {
    (void)gc_run(ftl, false);
}
```

這讓系統在空間降到門檻時先做 background GC，而不是等到完全失敗。

發生原因：

GC 本身會消耗暫時寫入空間。只把 free page 當成 host write 可用空間，忽略 GC migration 需求，就會在高壓寫入時卡住。

回歸測試：

`test_long_request_preserves_gc_migration_space()` 使用小容量設定與長 request，確認 GC 會發生、mapping 保持有效。

### 17.3 越界 LBA 造成 mapping table 風險

相關 API：

- `trace_request_is_in_range()`
- `ftl_request_range_is_valid()`
- `ftl_handle_request()`

問題：

Trace 是外部輸入。如果 `lba >= logical_pages` 或 `lba + length > logical_pages`，直接使用 LPN 當 mapping index 會越界。

修正：

入口與 FTL 各做一次檢查：

- `main.c` 在 replay trace 時先拒絕不合法 request。
- `ftl.c` 在實際處理 request 前再次檢查。

發生原因：

只在 main 擋錯誤還不夠，因為測試或未來其他入口可能直接呼叫 FTL API。核心模組本身也要保護自己的前置條件。

回歸測試：

`test_ftl_rejects_out_of_range_write()` 確認越界 request 會失敗，而且不會增加 host/nand write count。

### 17.4 設定檔錯誤被接受會讓結果不可信

相關 API：

- `ssd_config_load_file()`
- `parse_u32()`
- `ssd_config_validate()`

問題：

設定值如果被寬鬆解析，例如 `200us` 被當成 `200`，使用者可能不知道設定檔格式不正確。未知 key 如果被忽略，也會讓使用者以為某個設定生效，但其實沒有。

修正：

- `parse_u32()` 要求 value 全部都是數字。
- 未知 key 直接回傳錯誤。
- 載入後再做 geometry 與 GC threshold 驗證。

發生原因：

模擬器的輸出數字依賴設定。設定不明確時，效能統計就沒有可解釋性。

回歸測試：

- `test_config_load_rejects_malformed_entries()`
- `test_config_rejects_impossible_geometry()`

### 17.5 Latency accounting 需要一致定義

相關 API：

- `scheduler_run()`
- `stats_update_request()`
- `stats_average_latency_us()`

問題：

若 request submit time 比目前 device time 晚，scheduler 直接使用目前時間計算 dispatch，會讓 queue latency 語意不清楚。延遲統計也可能不滿足 `queue + service = total`。

修正：

`scheduler_run()` 先做時間對齊：

```c
if (ftl->current_time_us < request.submit_timestamp_us) {
    ftl->current_time_us = request.submit_timestamp_us;
}
```

發生原因：

模擬器沒有真實並行時間軸，必須明確定義 device time 如何追上 host submit time。

回歸測試：

`assert_latency_accounting_consistent()` 確認累積後仍符合：

```text
queue latency + service latency = total latency
```

## 18. API 使用準則

### 18.1 API 選擇總決策樹

```text
你現在要做什麼？
  |
  +-- 讀設定或檢查設定
  |     |
  |     +--> config API
  |          ssd_config_init_default()
  |          ssd_config_load_file()
  |          ssd_config_validate()
  |
  +-- 模擬 host 送出 write
  |     |
  |     +--> NVMe API
  |          nvme_submit_write()
  |
  +-- 讓 controller 從 SQ 取命令
  |     |
  |     +--> nvme_issue_pending()
  |          request_queue_enqueue()
  |
  +-- 執行韌體排程
  |     |
  |     +--> scheduler_run()
  |
  +-- 處理 LPN 到 PPA
  |     |
  |     +--> FTL / Mapping API
  |          ftl_handle_request()
  |          mapping_get_physical_page()
  |          mapping_set_physical_page()
  |
  +-- 改 NAND page 或 block 狀態
  |     |
  |     +--> NAND API
  |          nand_allocate_page()
  |          nand_program_page()
  |          nand_invalidate_page()
  |          nand_erase_block()
  |
  +-- 空間不足或想回收 block
  |     |
  |     +--> GC API
  |          gc_needed()
  |          gc_run()
  |
  +-- 輸出統計或算 WA
        |
        +--> Stats API
             stats_print()
             stats_export_csv()
             stats_write_amplification()
```

### 18.2 不同層級的入口 API

| 層級 | 從哪個 API 進入 | 適合用途 |
|------|-----------------|----------|
| CLI / Trace 層 | `replay_trace()` | 讀檔、解析 `WRITE`、提交到 NVMe |
| NVMe 層 | `nvme_submit_write()` | 模擬 host 送出命令 |
| Pipeline 層 | `service_nvme_pipeline()` | 把 SQ、RQ、CQ 串起來跑一次 |
| Scheduler 層 | `scheduler_run()` | 驅動 request queue，更新 latency 與 completion |
| FTL 層 | `ftl_handle_request()` | 驗證 LBA、寫入 LPN、更新 mapping |
| NAND 層 | `nand_program_page()` | 修改 page 狀態與 block counter |
| GC 層 | `gc_run()` | 執行 victim selection、migration、erase |

若只是寫測試，應選最接近測試目的的層級。要測 NVMe queue 行為，就從 `nvme_submit_write()` 開始；要測 FTL 邊界檢查，可以直接建 `request_t` 呼叫 `ftl_handle_request()`。

### 18.3 主機層不要跳過 NVMe API

若要新增 trace replay 或 workload generator，應使用：

```text
nvme_submit_write()
```

不要直接建立 `request_t` 丟進 request queue。直接丟 request 會跳過 command id、SQ count、submission count，也無法測到 NVMe queue 行為。

### 18.4 FTL 不直接改 NAND page state

應使用：

```text
nand_program_page()
nand_invalidate_page()
nand_erase_block()
```

不要直接寫：

```c
page->state = NAND_PAGE_VALID;
```

原因是 block 的 `valid_page_count`、`invalid_page_count`、`free_page_count` 也要一起維護。直接改 page state 很容易讓統計不一致。

### 18.5 GC 搬移資料後一定要更新 mapping

只要有效資料換位置，就要同步更新 L2P：

```text
new PPA program 成功
  |
  v
mapping_set_physical_page()
```

這是 FTL 正確性的核心規則。

### 18.6 設定檔要先 validate 再 init

正確順序：

```text
ssd_config_init_default()
ssd_config_load_file()    optional
ssd_config_validate()
request_queue_init()
nvme_controller_init()
ftl_init()
```

不要在 validate 前就呼叫 `ftl_init()`，否則可能用不合理 geometry 配置記憶體。

### 18.7 常見任務對照表

| 任務 | 建議呼叫順序 | 說明 |
|------|--------------|------|
| 跑一個 trace | `nvme_submit_write()` -> `service_nvme_pipeline()` | 保留完整 NVMe 到 FTL 流程 |
| 測 queue 滿的行為 | `nvme_submit_write()` 重複到滿 | 不必進入 FTL |
| 測 LBA 越界 | 建 `request_t` -> `ftl_handle_request()` | 直接測 FTL range check |
| 測 GC 後 mapping | 多筆 write -> `scheduler_run()` -> 檢查 `mapping_get_physical_page()` | 驗證 GC migration 正確性 |
| 測 WA | 跑會觸發 GC 的 workload -> `stats_write_amplification()` | WA 需要 host write 與 GC migration 才看得出差異 |
| 測設定檔錯誤 | `ssd_config_load_file()` | 不必初始化 FTL |
| 測 latency | `scheduler_run()` 後看 stats | latency 由 scheduler 統一計算 |

### 18.8 如何讀一個 API 的安全性

讀每個 API 時，可以照這個順序檢查：

1. 參數是否可能為 0 或空指標。
2. API 成功後改了哪些欄位。
3. API 失敗時是否保留原狀。
4. 有沒有同步更新 counter 或 metadata。
5. 呼叫者是否檢查回傳值。

以 `request_queue_enqueue()` 為例：

```text
前置條件:
queue 已 init，capacity > 0

成功後:
entries[tail] = request
tail 前進
size 增加

失敗時:
queue 已滿，回 false
不寫入 request
```

以 `nand_program_page()` 為例：

```text
成功後:
page state = VALID
page.logical_page_number = lpn
valid_page_count++
free_page_count--

注意:
目前函式沒有回傳失敗，所以呼叫者要先確保 PPA 來自 nand_allocate_page()
```

## 19. API 擴充建議

### 19.1 加入 Read Path

可能新增：

```c
bool ftl_handle_read(ftl_context_t *ftl, const request_t *request);
int nvme_submit_read(...);
```

流程：

```text
LPN -> mapping_get_physical_page()
  |
  +--> 找不到：回錯誤或回空資料語意
  |
  +--> 找到：current_time_us += read_latency_us
```

需要補的測試：

- 讀取已寫入 LPN。
- 讀取未寫入 LPN。
- GC 後讀取搬移過的 LPN。

### 19.2 加入 TRIM / Dataset Management

可能使用既有 `mapping_clear()`：

```text
TRIM LPN
  |
  +--> mapping_get_physical_page()
  +--> nand_invalidate_page()
  +--> mapping_clear()
```

這可以讓 invalid page 增加，進而影響 GC。

### 19.3 改進 Wear Leveling

可用現有欄位與 API：

- `nand_block_t.erase_count`
- `free_block_pool_get_min_erase_block()`

可能做法：

- 分配新 block 時優先挑 erase count 較小者。
- victim selection 同時考慮 invalid page count 與 erase count。

### 19.4 更完整的 NVMe Completion Polling

可擴充：

- Host 端保存 expected phase。
- `nvme_reap_completions()` 檢查 CQ entry phase。
- 避免誤讀舊 completion entry。

## 20. 快速索引

| 想查的功能 | 主要檔案 | 主要 API |
|------------|----------|----------|
| CLI 與 trace replay | `src/main.c` | `replay_trace()` |
| 設定檔 | `src/config.c` | `ssd_config_load_file()` |
| NVMe SQ/CQ | `src/nvme.c` | `nvme_submit_write()`、`nvme_post_completion()` |
| 內部佇列 | `src/request.c` | `request_queue_enqueue()`、`request_queue_dequeue()` |
| 排程與延遲 | `src/scheduler.c` | `scheduler_run()` |
| FTL 寫入 | `src/ftl.c` | `ftl_handle_request()` |
| L2P mapping | `src/mapping.c` | `mapping_get_physical_page()`、`mapping_set_physical_page()` |
| NAND page/block | `src/nand.c` | `nand_allocate_page()`、`nand_program_page()` |
| GC | `src/gc.c` | `gc_needed()`、`gc_run()` |
| Free block pool | `src/block_manager.c` | `free_block_pool_pop()`、`free_block_pool_push()` |
| 統計與 CSV | `src/stats.c` | `stats_print()`、`stats_export_csv()` |
| 回歸測試 | `tests/test_suite.c` | `test_*()` |
