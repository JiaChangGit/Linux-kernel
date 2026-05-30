# NVMe SSD 韌體寫入路徑模擬器

`ssd-fw-sim` 是一個以 C11 撰寫的 SSD 韌體模擬器。它聚焦在 **寫入路徑 (Write Path)**：主機送出 `WRITE` 指令後，資料如何經過 NVMe 佇列、韌體內部請求佇列、FTL 位址轉換、NAND 寫入、垃圾回收，最後回報完成。

這個專案不是完整 SSD 產品韌體，而是把 SSD 寫入流程拆成容易觀察的模組。讀程式時可以把它想成「精簡版控制器韌體」：

- **NVMe Queue (NVMe 佇列)**：模擬 Submission Queue 與 Completion Queue。
- **Request Queue (請求佇列)**：控制器內部用來暫存韌體請求。
- **FTL, Flash Translation Layer (快閃轉換層)**：把主機看到的 LBA/LPN 轉成 NAND 物理位置 PPA。
- **NAND Simulator (NAND 模擬層)**：維護 block/page 狀態，限制只能先擦除再寫入。
- **GC, Garbage Collection (垃圾回收)**：回收含有無效頁面的 block，釋放可重新使用的空間。
- **Statistics (統計資料)**：計算寫入放大、延遲、GC 次數等指標。

## 系統架構總覽

```text
Host Trace
  |
  |  WRITE <LBA> <SIZE>
  v
NVMe Submission Queue (SQ)
  |
  |  nvme_issue_pending()
  v
Firmware Request Queue
  |
  |  scheduler_run()
  v
FTL Write Path
  |       |
  |       +--> L2P Mapping Table
  |       +--> Garbage Collection (GC)
  v
NAND Block/Page Model
  |
  |  nvme_post_completion()
  v
NVMe Completion Queue (CQ)
  |
  |  nvme_reap_completions()
  v
Statistics / Result
```

重點觀念是：主機只知道邏輯位址，例如 LBA 100；SSD 內部實際寫到哪一個 block、哪一個 page，則由 FTL 決定。

## 快速開始

建置主程式：

```bash
make
```

執行範例 trace：

```bash
./ssd_fw_sim traces/sample.trace
```

執行測試：

```bash
make test
```

輸出 CSV：

```bash
./ssd_fw_sim --csv stats.csv traces/sample.trace
```

指定設定檔：

```bash
./ssd_fw_sim --config ssd.conf traces/sample.trace
```

清除建置產物：

```bash
make clean
```

## Trace 格式

目前 trace 只處理 `WRITE`：

```text
WRITE 0 4
WRITE 8 4
WRITE 0 2
```

欄位意義：

| 欄位 | 說明 |
|------|------|
| `WRITE` | 寫入操作，目前尚未實作 READ |
| `0` | 起始 LBA，也可視為本模擬器中的起始 LPN |
| `4` | 寫入頁數 |

範例 `WRITE 0 4` 代表寫入 LPN 0、1、2、3。若後面又出現 `WRITE 0 2`，FTL 會把 LPN 0、1 寫到新的物理頁，舊頁改成 invalid，這就是 **異地更新 (Out-of-place Update)**。

## 核心流程：一次寫入如何完成

以 `WRITE 100 4` 為例：

1. `nvme_submit_write()` 把指令放進 NVMe Submission Queue。
2. `nvme_issue_pending()` 把 SQ entry 轉成 `request_t`，放進韌體內部 Request Queue。
3. `scheduler_run()` 取出請求，記錄排隊時間與服務時間。
4. `ftl_handle_request()` 進入 FTL 寫入流程。
5. `ftl_handle_write()` 逐頁處理 LPN 100、101、102、103。
6. `nand_allocate_page()` 分配新的 PPA。
7. `nand_program_page()` 將頁面狀態從 free 改成 valid。
8. 若該 LPN 原本有舊 PPA，`nand_invalidate_page()` 將舊頁標成 invalid。
9. `mapping_set_physical_page()` 更新 L2P mapping。
10. `nvme_post_completion()` 回報完成，`nvme_reap_completions()` 模擬主機取走完成項。

## FTL 設計

本專案採用 **頁級映射 (Page-level Mapping)**。

| 設計 | 說明 |
|------|------|
| LPN | Logical Page Number，主機看到的邏輯頁編號 |
| PPA | Physical Page Address，NAND 內部的 block/page 位置 |
| L2P | Logical-to-Physical Mapping，LPN 到 PPA 的對照表 |
| Out-of-place Update | 不覆寫舊頁，而是寫到新頁，再讓舊頁失效 |

寫入順序：

```text
查舊 mapping
  |
分配新 page
  |
寫入新 page
  |
舊 page 標記 invalid
  |
更新 L2P mapping
```

為什麼不直接覆寫舊 page？因為 NAND page 通常不能像 DRAM 一樣原地改寫，必須等整個 block erase 後才能重新使用。

## NAND 狀態模型

每個 block 包含多個 page，page 狀態只允許照下列方向變化：

```text
FREE -> VALID -> INVALID
  ^                 |
  |                 |
  +--- Block Erase -+
```

狀態說明：

| 狀態 | 英文 | 意義 |
|------|------|------|
| 空閒 | `FREE` | page 尚未被寫入，可以 program |
| 有效 | `VALID` | page 目前保存某個 LPN 的最新資料 |
| 無效 | `INVALID` | page 內容已過期，等 GC 回收 |

## Garbage Collection

當 free block 數量低於 `gc_free_block_threshold` 時，系統會嘗試執行 GC。

目前的 GC 策略是 **貪婪策略 (Greedy Policy)**：選擇 invalid page 最多的 block 作為 victim block。這樣通常可以一次回收較多空間，但還沒有把 erase count 納入考量，所以不等於完整的 **磨耗平均 (Wear Leveling)**。

GC 流程：

```text
選 victim block
  |
搬移 victim 內仍有效的 page
  |
更新 L2P mapping
  |
擦除 victim block
  |
把 block 放回 free block pool
```

GC 延遲會加進模擬時間：

| 操作 | 預設延遲 |
|------|----------|
| NAND Read | 50 us |
| NAND Program | 200 us |
| NAND Erase | 1500 us |

## 設定檔

`ssd.conf` 使用 `key=value` 格式：

```text
total_blocks=128
pages_per_block=64
logical_pages=4096
request_queue_depth=256
gc_free_block_threshold=8
read_latency_us=50
program_latency_us=200
erase_latency_us=1500
trace_inter_arrival_us=10
```

設定限制：

- `total_blocks`、`pages_per_block`、`logical_pages`、`request_queue_depth` 不可為 0。
- `logical_pages` 不可大於 `total_blocks * pages_per_block`。
- `gc_free_block_threshold` 必須介於 1 到 `total_blocks - 1`。
- 未知 key、格式錯誤、非純數字值會載入失敗。

## 統計指標

執行後會印出：

| 指標 | 英文 | 說明 |
|------|------|------|
| Host Requests | Host Requests | 主機送出的寫入請求數 |
| Host Pages | Host Pages | 主機要求寫入的頁數 |
| NAND Writes | NAND Writes | 實際 NAND program 次數，包含 GC 搬移 |
| NAND Reads | NAND Reads | GC 搬移有效頁時產生的 NAND read |
| NAND Erases | NAND Erases | block erase 次數 |
| GC Count | GC Count | GC 執行次數 |
| Migrated Pages | Migrated Pages | GC 搬移的有效頁數 |
| Write Amplification | WA | NAND 寫入頁數 / Host 寫入頁數 |
| Queue Latency | Queue Latency | request 進入佇列到被 scheduler 處理的時間 |
| Service Latency | Service Latency | request 開始處理到完成的時間 |
| Total Latency | Total Latency | submit 到 complete 的總時間 |

寫入放大範例：

```text
Host 寫入 10 pages
GC 額外搬移 5 pages
NAND Writes = 15
Write Amplification = 15 / 10 = 1.5
```

WA 越高，代表同樣的 host 寫入量造成更多 NAND program，通常會帶來較高延遲與較多磨耗。

## 開發過程中處理過的 Bug

### Bug 1：GC 後 mapping 可能指到舊位置

情境：

- 某個 block 被 GC 選為 victim。
- victim 裡仍有 valid page。
- GC 把 valid page 搬到新 PPA。
- 若 L2P mapping 沒同步更新，之後查詢該 LPN 會指到已被 erase 的舊 PPA。

解法：

- 在 `gc_migrate_valid_pages()` 中，valid page program 到新 PPA 後，立即呼叫 `mapping_set_physical_page()`。
- 測試 `test_gc_preserves_valid_mappings()` 會在 GC 後檢查每個 LPN 的 mapping 是否仍指向 valid page。

發生原因：

- GC 不是單純 erase block，它會改變有效資料的位置。
- 只搬 page 不更新 mapping，FTL 的邏輯視角就會和 NAND 物理狀態不一致。

### Bug 2：長請求可能吃完 GC 搬移所需空間

情境：

- 一次 request 寫入很多 page，例如 `WRITE 8 8`。
- 寫入過程中 free block pool 逐漸降低。
- 若等到完全 allocate 失敗才 GC，GC 搬移 victim valid page 時可能沒有新 page 可用。

解法：

- 在 `ftl_handle_write()` 逐頁寫入時，每次 allocate 前先檢查 `gc_needed()`。
- 若 free block 數量低於門檻，先做 background GC，保留搬移空間。
- 測試 `test_long_request_preserves_gc_migration_space()` 驗證長請求下 GC 後 mapping 仍完整。

發生原因：

- GC 本身也需要寫入空間。若把所有 free page 都給 host write 用掉，GC 就沒有地方搬移 valid page。

### Bug 3：延遲統計可能出現不合理數值

情境：

- trace 會用 `trace_inter_arrival_us` 模擬每筆 request 的抵達時間。
- 如果韌體目前時間比下一筆 request 的 submit time 還早，直接相減可能讓 queue latency 語意錯誤。

解法：

- `scheduler_run()` 在計算 latency 前，先把 `current_time_us` 對齊到 `request.submit_timestamp_us`。
- 測試中的 `assert_latency_accounting_consistent()` 檢查 `queue + service == total`。

發生原因：

- 模擬器沒有真實時鐘，時間由程式手動推進。
- request 抵達時間與 device service time 要先對齊，延遲統計才有一致意義。

## 目前限制

- 只支援 `WRITE` trace，尚未實作 read path。
- 單執行緒同步模擬，沒有 mutex、interrupt、DMA。
- GC 是同步呼叫，background GC 只是統計分類，沒有獨立背景執行緒。
- 沒有完整 wear leveling。
- 沒有 power-loss recovery、checkpoint、metadata journal。
- NVMe 只模擬 SQ/CQ 與 phase bit，沒有完整 admin command 或真實 PCIe doorbell。

## 後續可擴充方向

- 加入 read path 與 read latency。
- 加入 hot/cold data placement，降低 GC 搬移成本。
- 將 victim selection 從 greedy 改為 cost-benefit 或 erase-count-aware。
- 加入 wear leveling，避免少數 block 過度 erase。
- 模擬 multi-channel / multi-die NAND 平行度。
- 加入 power-loss checkpoint 與 metadata journal。
- 補上更完整的 NVMe admin command 與 doorbell 模型。
