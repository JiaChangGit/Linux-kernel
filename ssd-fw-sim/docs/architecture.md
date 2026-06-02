# 架構筆記 (Architecture Notes)

`ssd-fw-sim` 是單執行緒的 SSD write path 模擬器。它不是完整 SSD 產品韌體，而是把主機命令、韌體內部佇列、FTL、NAND、GC 與統計拆成可以逐層追蹤的 C 模組。

## 模組分工

| 模組 | English | 責任 | 主要 API | 為什麼這樣拆 |
| --- | --- | --- | --- | --- |
| `nvme.c` | NVMe Queue Model | 模擬 Submission Queue (SQ) 與 Completion Queue (CQ)。 | `nvme_submit_write()`、`nvme_issue_pending()`、`nvme_post_completion()` | 保留 host/controller 邊界，方便觀察 queue depth 與 completion。 |
| `request.c` | Firmware Request Queue | 韌體內部 request ring queue。 | `request_queue_enqueue()`、`request_queue_dequeue()` | 把 NVMe command 轉成韌體工作，不讓 FTL 直接碰 SQ。 |
| `scheduler.c` | Request Scheduler | 從 request queue dispatch 到 FTL，計算 latency。 | `scheduler_run()` | 集中 queue latency、service latency、completion 的計算。 |
| `ftl.c` | Flash Translation Layer | 處理 LPN -> PPA、write path、GC 觸發。 | `ftl_handle_request()` | 讓主流程只知道 request，不直接操作 NAND 細節。 |
| `mapping.c` | L2P Mapping Table | 管理 LPN 到 PPA 的 mapping。 | `mapping_get_physical_page()`、`mapping_set_physical_page()` | GC 和 write path 都會改 mapping，集中 API 較不易不一致。 |
| `nand.c` | NAND Block/Page Model | 維護 FREE / VALID / INVALID 狀態。 | `nand_allocate_page()`、`nand_program_page()`、`nand_invalidate_page()`、`nand_erase_block()` | 用狀態機呈現 NAND 不能原地覆寫的限制。 |
| `gc.c` | Garbage Collection | 選 victim、搬 valid page、erase block。 | `gc_needed()`、`gc_run()` | 將空間回收邏輯獨立出來，便於測 WA 和長尾延遲。 |
| `stats.c` | Telemetry | 統計 request、latency、WA、GC 次數。 | `stats_update_request()`、`stats_print()`、`stats_export_csv()` | 讓模擬結果可觀測，也方便輸出 CSV 比較。 |
| `block_manager.c` | Free Block Pool | 管理可寫入 block pool。 | `free_block_pool_push()`、`free_block_pool_pop()` | 把 free block 的生命週期和 NAND 狀態分開。 |

## API 選擇重點

| 選項 | 適合情境 | 本專案選擇 |
| --- | --- | --- |
| 直接讓 `main.c` 呼叫 FTL | 最小 demo，流程短。 | 沒採用。會看不到 NVMe SQ/CQ 與 queue latency。 |
| `nvme.c` + `request.c` 分層 | 想觀察 host command 和 firmware request 的差別。 | 採用。新手可清楚看到「提交」不等於「已處理」。 |
| 每個模組直接改 struct 欄位 | 短期寫得快。 | 盡量避免。透過 API 修改狀態，較容易維持一致性。 |
| 單執行緒模型 | 教學、可重現、方便算 latency。 | 採用。文件要說清楚目前沒有多 queue 或多執行緒。 |

## 新手閱讀順序

1. 先看 `main.c`：trace 怎麼變成 `nvme_submit_write()`。
2. 再看 `nvme.c`：SQ entry 怎麼變成 `request_t`。
3. 接著看 `scheduler.c`：request 何時 dispatch、latency 怎麼算。
4. 最後看 `ftl.c`、`nand.c`、`gc.c`：資料如何 program、舊頁如何 invalid、GC 如何搬移 valid page。
