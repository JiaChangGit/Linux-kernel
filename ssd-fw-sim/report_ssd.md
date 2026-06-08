# NVMe SSD 韌體寫入路徑模擬器技術報告

## 1. 專案定位

`ssd-fw-sim` 是一個以 C11 撰寫的 SSD 韌體寫入路徑模擬器。它把一筆主機寫入請求拆成幾個在真實 SSD 控制器中常見的階段：NVMe 佇列、韌體請求佇列、排程器、FTL 位址轉換、NAND page program、GC 回收，以及完成回報。

這份報告聚焦在每個模組負責什麼、資料如何流動、為什麼要這樣設計，以及開發過程中遇到哪些邊界問題。

## 2. 先理解幾個關鍵字

| 中文 | 英文 | 說明 |
|------|------|------|
| 主機 | Host | 發出讀寫請求的一端，例如作業系統或測試 trace |
| 控制器 | Controller | SSD 內部處理 NVMe 指令與 NAND 存取的韌體/硬體邏輯 |
| 韌體 | Firmware | 介於主機協定與 NAND 物理限制之間的控制程式 |
| LBA | Logical Block Address | 主機看到的邏輯位址，本專案把它簡化成 page 單位 |
| LPN | Logical Page Number | 邏輯頁編號，FTL 查表時使用 |
| PPA | Physical Page Address | 物理頁位址，由 block index 與 page index 組成 |
| FTL | Flash Translation Layer | 將 LPN 對應到 PPA 的快閃轉換層 |
| L2P | Logical-to-Physical Mapping | LPN 到 PPA 的 mapping table |
| SQ | Submission Queue | NVMe 主機提交指令的佇列 |
| CQ | Completion Queue | NVMe 控制器回報完成結果的佇列 |
| GC | Garbage Collection | 搬移有效頁、擦除含垃圾頁 block 的回收流程 |
| WA | Write Amplification | NAND 實際寫入量除以 host 寫入量 |

SSD 的核心難點來自 NAND 的物理限制：page 可以 program，但不能像一般記憶體一樣直接覆寫；block 要先 erase，裡面的 page 才能再次使用。因此 FTL 需要用 **異地更新 (Out-of-place Update)** 處理覆寫。

## 3. 專案架構

```text
main.c
  |
  +--> config.c        讀取與驗證 ssd.conf
  +--> nvme.c          模擬 SQ/CQ
  +--> request.c       韌體內部 request queue
  +--> scheduler.c     取出 request，計算 latency
  +--> ftl.c           寫入路徑與 L2P 更新
  |      |
  |      +--> mapping.c
  |      +--> nand.c
  |      +--> gc.c
  |
  +--> stats.c         統計與 CSV 輸出
```

各模組刻意拆開，原因是 SSD 韌體通常也會把主機協定、FTL、NAND 管理、統計與設定分層處理。這樣做可以讓每個模組的責任清楚，也讓測試可以針對單一層級驗證。

## 4. 寫入路徑總覽

以下是 `WRITE 100 4` 的完整流程：

```text
Trace: WRITE 100 4
  |
  v
nvme_submit_write()
  |  產生 command_id，寫入 SQ
  v
nvme_issue_pending()
  |  SQ entry -> request_t
  v
request_queue_enqueue()
  |
  v
scheduler_run()
  |  計算 queue latency，呼叫 FTL
  v
ftl_handle_request()
  |
  v
ftl_handle_write()
  |  逐頁處理 LPN 100, 101, 102, 103
  v
nand_allocate_page()
  |
  v
nand_program_page()
  |
  +--> 若 LPN 有舊 PPA，nand_invalidate_page()
  |
  v
mapping_set_physical_page()
  |
  v
nvme_post_completion()
  |
  v
nvme_reap_completions()
```

這個流程可以分成三個視角：

- **Host 視角**：送出一筆 `WRITE 100 4`，最後收到 completion。
- **FTL 視角**：把 LPN 100 到 103 映射到新的 PPA。
- **NAND 視角**：新的 page 被 program，舊 page 被標成 invalid，之後等待 GC 回收。

## 5. NVMe 佇列模型

本專案的 NVMe 模型由 `src/nvme.c` 實作，主要資料結構是 `nvme_controller_t`。

### 5.1 Submission Queue

`nvme_submit_write()` 模擬 host 把 write command 放進 SQ：

- 分配 `command_id`。
- 記錄 `slba`、`nlb`、`submit_timestamp_us`。
- 更新 `sq_tail` 與 `sq_count`。
- SQ 滿時回傳失敗。

這裡的 `nlb` 在真實 NVMe 中常代表 number of logical blocks。本模擬器為了簡化，把它當成要寫入的 page 數。

### 5.2 Firmware Request Queue

`nvme_issue_pending()` 會把 SQ entry 轉成 `request_t`，再放進 `request_queue_t`。

為什麼不在 `nvme_submit_write()` 直接執行 FTL？原因是 SQ 是 host 與 controller 的協定介面，request queue 是韌體內部工作佇列。兩層分開後，可以觀察「host 已提交」和「韌體開始處理」之間的差異，也就是 queue latency。

### 5.3 Completion Queue

`nvme_post_completion()` 把執行結果放進 CQ，`nvme_reap_completions()` 模擬 host 把完成項取走。

目前有模擬 `phase` bit 的翻轉：

```text
CQ tail 回到 0
  |
  v
cq_phase ^= 1
```

但 host 端沒有檢查 phase，只是把 CQ 內所有完成項 reap 掉。這是簡化模型，適合用來觀察完成流程，不等於完整 NVMe 驅動程式。

## 6. Scheduler 與延遲模型

`scheduler_run()` 負責從 request queue 取出請求，呼叫 FTL，並更新統計。

延遲分成三種：

| 指標 | 公式 | 意義 |
|------|------|------|
| Queue Latency | `dispatch - submit` | 請求在佇列中等待多久 |
| Service Latency | `complete - dispatch` | 韌體與 NAND 實際處理多久 |
| Total Latency | `complete - submit` | 從 host submit 到完成的總時間 |

模擬器使用 `current_time_us` 表示裝置端時間。因為沒有真實 clock，所有時間都由程式在 NAND read/program/erase 時手動增加。

重要細節：

```c
if (ftl->current_time_us < request.submit_timestamp_us) {
    ftl->current_time_us = request.submit_timestamp_us;
}
```

這段表示：如果裝置已經把前面工作做完，但下一筆請求還沒抵達，就把裝置時間推進到 submit time。否則 queue latency 會失去意義。

## 7. FTL 寫入設計

FTL 的入口是 `ftl_handle_request()`，目前只支援 `REQUEST_TYPE_WRITE`。

### 7.1 Page-level Mapping

本專案使用 **頁級映射 (Page-level Mapping)**：

```text
LPN 0 -> PPA(block 0, page 0)
LPN 1 -> PPA(block 0, page 1)
LPN 2 -> PPA(block 3, page 5)
```

優點：

- 隨機寫入彈性高。
- 單一 LPN 更新時，只需要搬動該 LPN 的 mapping。
- GC 搬移 valid page 後，可以逐頁修正 L2P。

缺點：

- Mapping table 需要較多記憶體。
- 真實 SSD 會需要 metadata checkpoint 或 journal 才能在斷電後恢復。

### 7.2 Out-of-place Update

當同一個 LPN 被重複寫入時，FTL 不覆寫舊 PPA，而是分配新 PPA：

```text
第一次 WRITE 0 1:
  LPN 0 -> PPA(0,0), page state = VALID

第二次 WRITE 0 1:
  新 PPA(0,1) 寫入 LPN 0
  舊 PPA(0,0) 標成 INVALID
  L2P 更新為 LPN 0 -> PPA(0,1)
```

這個順序很重要：

1. 先寫新 page。
2. 再讓舊 page invalid。
3. 最後更新 mapping。

在真實系統中，這個順序會影響斷電一致性。本專案沒有實作 power-loss recovery，但仍保留類似的更新順序，讓邏輯更貼近實務。

### 7.3 邏輯範圍檢查

`ftl_request_range_is_valid()` 會檢查：

- `lba` 是否超出 `logical_pages`。
- `lba + length` 是否 overflow。
- `lba + length` 是否超出 logical range。

若 trace 裡有 `WRITE 4096 1`，而 `logical_pages=4096`，這筆 request 會被拒絕，避免 mapping table 越界。

## 8. NAND 模擬層

`nand.c` 模擬 block/page 狀態，不模擬資料內容本身。每個 page 記錄：

- `state`
- `logical_page_number`
- `has_logical_page`

`logical_page_number` 可以理解成簡化的 **OOB / Spare Area Metadata**。GC 搬移 valid page 時，需要知道這個 physical page 原本屬於哪個 LPN，才能更新 mapping。

### 8.1 Page 狀態

```text
FREE
  |
  | nand_program_page()
  v
VALID
  |
  | nand_invalidate_page()
  v
INVALID
  |
  | nand_erase_block()
  v
FREE
```

`nand_allocate_page()` 只負責挑出下一個可寫的位置；真正讓 page 從 FREE 變 VALID 的是 `nand_program_page()`。

### 8.2 Block 內順序寫入

每個 block 有 `write_pointer`：

```text
block 0:
page 0  page 1  page 2  page 3
  ^ 
write_pointer
```

每次 allocate page 後，`write_pointer` 會往後移。若目前 block 已滿，就從 free block pool 取下一個 block 當作 `current_write_block`。

## 9. Garbage Collection 設計

GC 的入口是 `gc_run()`。觸發條件由 `gc_needed()` 判斷：

```text
free_block_pool_count < gc_free_block_threshold
```

### 9.1 Victim Selection

目前使用 **Greedy Policy**：

```text
選 invalid_page_count 最大的 block
跳過 current_write_block
```

範例：

| Block | Valid Pages | Invalid Pages | 是否適合當 victim |
|-------|-------------|---------------|-------------------|
| 0 | 2 | 6 | 適合 |
| 1 | 7 | 1 | 不太適合 |
| 2 | 0 | 8 | 最適合 |

選 invalid page 多的 block，可以減少需要搬移的 valid page 數量。例如 block 2 全部都是 invalid，GC 可以直接 erase，幾乎沒有搬移成本。

### 9.2 Valid Page Migration

GC 不能直接 erase 一個還有 valid page 的 block。流程如下：

```text
victim block
  |
  +-- page 0 INVALID  -> 不搬
  +-- page 1 VALID    -> 讀出，搬到新 PPA，更新 L2P
  +-- page 2 INVALID  -> 不搬
  +-- page 3 VALID    -> 讀出，搬到新 PPA，更新 L2P
  |
  v
erase victim block
```

搬移一個 valid page 的成本：

- `read_latency_us`
- `program_latency_us`
- `nand_read_count++`
- `nand_write_count++`
- `migrated_page_count++`

這也是 WA 會上升的主要原因。

### 9.3 Foreground GC 與 Background GC

本專案用參數區分兩種 GC：

| 類型 | 英文 | 觸發情境 | 影響 |
|------|------|----------|------|
| 前台 GC | Foreground GC | allocate page 失敗後被迫回收 | 會直接擋住 host request |
| 背景 GC | Background GC | free block 低於門檻時預先回收 | 仍是同步執行，但統計上與前台 GC 分開 |

目前的 background GC 沒有獨立 thread。它代表「在流程中預先做 GC」，不是作業系統背景工作。

## 10. 統計與效能觀察

`stats.c` 收集以下資訊：

- Host request/page 數量。
- NAND read/write/erase 次數。
- GC 次數與搬移頁數。
- Sequential/random write 數量。
- Queue/service/total latency。
- Write amplification。

### 10.1 Write Amplification

公式：

```text
Write Amplification = NAND Writes / Host Pages
```

例子：

```text
Host 寫入 20 pages
GC 搬移 6 valid pages
NAND Writes = 26
WA = 26 / 20 = 1.3
```

WA 不只反映效能，也反映磨耗。WA 越高，代表同樣的 host workload 讓 NAND 做了更多實際 program。

### 10.2 Sequential 與 Random 判斷

程式用 `last_write_end_lpn` 判斷 request 是否連續：

```text
上一筆：WRITE 0 4  -> 結尾是 LPN 4
下一筆：WRITE 4 2  -> sequential
下一筆：WRITE 8 1  -> random
```

這個判斷是 request-level，不是 page-level。第一次非空寫入會被歸類為 random，因為還沒有上一筆可比較。

## 11. 設定與驗證

`config.c` 負責預設值、設定檔載入與合法性檢查。

### 11.1 預設設定

| Key | 預設值 | 說明 |
|-----|--------|------|
| `total_blocks` | 128 | NAND block 數 |
| `pages_per_block` | 64 | 每個 block 的 page 數 |
| `logical_pages` | 4096 | host 可見的邏輯 page 數 |
| `request_queue_depth` | 256 | SQ、CQ、內部 request queue 深度 |
| `gc_free_block_threshold` | 8 | free block 低於此值就觸發 GC |
| `read_latency_us` | 50 | NAND read 延遲 |
| `program_latency_us` | 200 | NAND program 延遲 |
| `erase_latency_us` | 1500 | block erase 延遲 |
| `trace_inter_arrival_us` | 10 | trace request 之間的抵達間隔 |

### 11.2 設定檔錯誤處理

以下情況會失敗：

- 缺少 `=`。
- value 不是純數字，例如 `200us`。
- key 不在白名單內。
- `logical_pages` 超過物理容量。
- `gc_free_block_threshold` 為 0 或大於等於 `total_blocks`。

這樣做的目的是讓錯誤早點發生。若設定不合理還繼續跑，後面可能變成 mapping 越界或 GC 無法回收，會比較難除錯。

## 12. 測試策略

`tests/test_suite.c` 使用 `assert` 寫回歸測試。測試除了確認程式能跑完，也會檢查狀態是否一致。

| 測試 | 驗證重點 |
|------|----------|
| `test_config_rejects_impossible_geometry` | 不合理 geometry 會被拒絕 |
| `test_config_load_rejects_malformed_entries` | 設定檔格式錯誤會失敗 |
| `test_nvme_sq_cq_lifecycle` | SQ/CQ 入隊、出隊、滿佇列行為 |
| `test_scheduler_pipeline_posts_completions` | scheduler 會產生 completion 並更新 stats |
| `test_ftl_rejects_out_of_range_write` | LBA 越界不會寫入 NAND |
| `test_gc_preserves_valid_mappings` | GC 後 L2P 仍指向 valid page |
| `test_long_request_preserves_gc_migration_space` | 長 request 下仍保留 GC 搬移空間 |

執行：

```bash
make test
```

## 13. 開發過程中的 Bug 與修正

### 13.1 Bug：GC 搬移後 L2P mapping 不一致

問題現象：

- GC 選到 victim block。
- victim block 內有 valid page。
- valid page 被搬到新的 PPA。
- 若 mapping table 沒更新，LPN 仍指向舊 PPA。
- victim block erase 後，舊 PPA 變回 free，mapping 卻仍指到那裡。

影響：

- 後續若讀取該 LPN，會讀到錯誤位置。
- 即使本專案目前沒有 read path，mapping 不一致仍會破壞 FTL 狀態。

原因分析：

- GC 搬移資料時，物理位置改變。
- FTL 的 L2P table 是邏輯視角的唯一來源。
- 搬移有效頁但沒有更新 L2P，就等於 NAND 與 FTL 各自記著不同版本的事實。

修正方式：

- 在 `gc_migrate_valid_pages()` 中，每搬完一個 valid page，就呼叫：

```c
mapping_set_physical_page(ftl->mapping_table,
                          page->logical_page_number,
                          &new_ppa);
```

驗證方式：

- `test_gc_preserves_valid_mappings()` 先製造 invalid page，迫使 GC 發生。
- GC 後逐一檢查 LPN 0 到 15。
- 每個 LPN 的 mapping 都必須存在，而且對應 page 狀態必須是 `NAND_PAGE_VALID`。

### 13.2 Bug：長 request 導致 GC 沒有搬移空間

問題現象：

- 小 geometry 下，連續寫入會快速消耗 free block。
- 如果一筆 request 很長，例如一次寫 8 pages，它可能在單次 FTL loop 裡吃掉最後的 free page。
- allocate 失敗後才做 foreground GC 時，GC 要搬 valid page，卻可能找不到可寫的新 page。

原因分析：

- GC 除了 erase，也可能需要 migration。
- migration 需要新的 page 存放 valid data。
- 如果 free block pool 已經被 host write 消耗到太低，GC 就無法安全完成。

修正方式：

- 在 `ftl_handle_write()` 的每個 LPN 寫入前先檢查 `gc_needed()`。
- 低於門檻就先呼叫 `gc_run(ftl, false)`。
- 這讓系統在還有空間時先回收，不等到完全失敗。

驗證方式：

- `test_long_request_preserves_gc_migration_space()` 使用較小的 NAND geometry。
- 先製造舊資料與 invalid page，再送出長 request。
- 測試要求 GC 發生、background GC count 大於 0，且所有 LPN mapping 仍有效。

### 13.3 Bug：越界 request 可能造成 mapping table 存取風險

問題現象：

- 若 trace 出現超過 `logical_pages` 的 LBA，FTL 若直接使用它當 index，會造成 mapping table 越界。

原因分析：

- Trace 是外部輸入，不能假設永遠正確。
- `lba + length` 還要注意 unsigned integer overflow。

修正方式：

- `main.c` 的 `trace_request_is_in_range()` 在 replay 階段先擋一次。
- `ftl.c` 的 `ftl_request_range_is_valid()` 在 FTL 層再擋一次。
- 這是兩層防線：入口檢查與核心模組檢查。

驗證方式：

- `test_ftl_rejects_out_of_range_write()` 建立 LBA 超出範圍的 request。
- 預期 `ftl_handle_request()` 回傳 false，且 host/nand write 統計都不增加。

### 13.4 Bug：設定檔格式錯誤不應被默默接受

問題現象：

- 如果 `program_latency_us=200us` 被當成 200，使用者可能不知道設定其實寫錯。
- 如果未知 key 被忽略，實際執行設定可能和使用者以為的不一樣。

原因分析：

- 模擬器的結果高度依賴設定值。
- 設定錯誤若沒有 fail fast，後續效能數字會失去可信度。

修正方式：

- `parse_u32()` 要求整個 value 都必須是數字。
- `ssd_config_load_file()` 遇到未知 key 直接回傳失敗。
- `ssd_config_validate()` 檢查 geometry 與 GC threshold。

驗證方式：

- `test_config_load_rejects_malformed_entries()` 測 `200us` 與 `mystery_key`。
- `test_config_rejects_impossible_geometry()` 測 logical capacity 與 GC threshold。

### 13.5 Bug：延遲統計需要維持 `queue + service = total`

問題現象：

- 若 submit time、dispatch time、complete time 沒有一致定義，統計可能看起來合理，但三者加總對不起來。

原因分析：

- 模擬器用同一個 `current_time_us` 推進裝置時間。
- Host request 可以用 `trace_inter_arrival_us` 表示抵達間隔。
- Scheduler 必須先處理「裝置時間是否落後於 request submit time」。

修正方式：

- Scheduler 在計算 dispatch 前先對齊時間。
- 完成後用同一筆 request 的 timestamp 算三種 latency。

驗證方式：

- `assert_latency_accounting_consistent()` 檢查：

```text
total_queue_latency_us + total_service_latency_us == total_latency_us
```

## 14. 可觀察的輸出範例

執行：

```bash
./ssd_fw_sim traces/sample.trace
```

會看到設定與統計：

```text
=== SSD Configuration ===
total_blocks           : 128
pages_per_block        : 64
logical_pages          : 4096
request_queue_depth    : 256
gc_free_block_threshold: 8
read_latency_us        : 50
program_latency_us     : 200
erase_latency_us       : 1500
trace_inter_arrival_us : 10

=== SSD Statistics ===
Host Requests          : 7
Host Pages             : 46
NAND Writes            : 46
NAND Reads             : 0
NAND Erases            : 0
GC Count               : 0
Migrated Pages         : 0
Write Amplification    : 1.00
```

這個 sample trace 沒有把 NAND 壓到需要 GC，所以 WA 是 1.00。若使用較小的 `total_blocks` 與較多覆寫，就會看到 GC、NAND Reads、NAND Erases、Migrated Pages 上升。

## 15. 限制與後續方向

目前限制：

- 只有 write path，沒有 read path。
- 單執行緒同步模擬，沒有 interrupt、DMA、pthread。
- Background GC 是同步流程中的預先回收，不是獨立背景執行緒。
- Victim selection 只看 invalid page 數，沒有 wear leveling。
- Mapping table 只在記憶體中，沒有 checkpoint 或 journal。
- NVMe 只模擬 SQ/CQ 核心概念，沒有完整 PCIe doorbell 與 admin command。

後續方向：

- 增加 read path，讓 L2P 查詢與 NAND read latency 可觀察。
- 加入 hot/cold data separation，降低 GC 搬移有效頁的成本。
- 改進 GC policy，例如 cost-benefit 或 erase-count-aware。
- 加入 wear leveling，避免少數 block erase 次數過高。
- 模擬 multi-channel / multi-die NAND 平行度。
- 設計 metadata checkpoint 與 journal，處理斷電恢復。
- 補上更完整的 NVMe command set 與 doorbell 模型。
