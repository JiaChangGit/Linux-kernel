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

## 快速開始（最短流程）

這段適合先確認環境能不能跑起來。若你第一次看這個專案，建議先照順序跑一次，再往下看「建置流程詳解」與「DEMO 流程」。

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

## 建置流程詳解

建置流程的目的，是把 `src/*.c` 原始碼編譯成可執行檔 `ssd_fw_sim`，並確認程式在嚴格編譯選項下沒有警告或錯誤。

### 建置相關關鍵字

| 關鍵字 | 英文 | 涵義 |
|--------|------|------|
| 建置 | Build | 將原始碼轉成可執行檔的完整流程 |
| 編譯 | Compile | 將單一 `.c` 檔轉成 `.o` 物件檔 |
| 連結 | Link | 將多個 `.o` 物件檔合成執行檔 |
| 目標 | Target | Makefile 中可執行的任務，例如 `all`、`test`、`clean` |
| 物件檔 | Object File | 編譯後的中間檔，副檔名通常是 `.o` |
| 執行檔 | Executable | 可以直接執行的程式，本專案是 `ssd_fw_sim` |
| 編譯旗標 | Compiler Flags | 控制編譯規則的參數，例如 `-Wall`、`-Werror` |
| 回歸測試 | Regression Test | 確認修改後舊功能沒有壞掉的測試 |

### Makefile 主要目標

| 指令 | 目的 | 為什麼要做 |
|------|------|------------|
| `make` | 建置主程式 `ssd_fw_sim` | 產生可執行的 SSD 模擬器 |
| `make test` | 建置並執行 `ssd_fw_sim_tests` | 確認 queue、FTL、GC、config、latency 邏輯仍正確 |
| `make clean` | 刪除 `build/`、`ssd_fw_sim`、`ssd_fw_sim_tests` | 回到乾淨狀態，避免舊物件檔影響判斷 |

### 建置流程圖

```text
include/*.h      src/*.c
     |              |
     +------ make --+
              |
              v
        build/*.o
              |
              v
        ssd_fw_sim
```

`include/*.h` 是標頭檔，定義資料結構與 API；`src/*.c` 是實作檔。`make` 會先把每個 `.c` 編譯成 `build/*.o`，再把所有物件檔連結成 `ssd_fw_sim`。

### Step 1：建置主程式

```bash
make
```

目的：

- 產生 `ssd_fw_sim`。
- 確認所有 C 原始碼能通過 `-std=c11 -Wall -Wextra -Werror -pedantic`。

原因：

- `-Wall` 和 `-Wextra` 會打開較多警告。
- `-Werror` 會把警告視為錯誤，避免小問題被忽略。
- `-pedantic` 會讓程式更接近標準 C，不依賴太多編譯器特例。

成功後，專案根目錄會出現：

```text
ssd_fw_sim
build/
```

### Step 2：執行回歸測試

```bash
make test
```

目的：

- 建置 `ssd_fw_sim_tests`。
- 執行 `tests/test_suite.c` 內的測試案例。

這些測試會檢查：

| 測試方向 | 檢查重點 |
|----------|----------|
| Config | 不合法 SSD 幾何與設定檔格式會被拒絕 |
| NVMe SQ/CQ | submission queue 與 completion queue 的滿佇列、出隊、完成流程 |
| Scheduler | request 會被處理並產生 completion |
| FTL | 越界 LBA 不會寫入 NAND |
| GC | 搬移 valid page 後 L2P mapping 仍指向有效 page |
| Latency | `queue latency + service latency = total latency` |

原因：

- SSD 模擬器有多個狀態表，例如 SQ/CQ、L2P mapping、NAND page state、free block pool。
- 只看程式能編譯不夠，還要確認狀態轉換沒有壞掉。

成功時會看到：

```text
All tests passed
```

### Step 3：清除建置產物

```bash
make clean
```

目的：

- 刪除編譯後的中間檔與執行檔。

原因：

- 若你想確認專案能從零開始建置，先 `make clean` 再 `make` 最清楚。
- 若要提交程式碼，通常不需要把 `build/` 或執行檔一起提交。

## DEMO 流程

DEMO 的目的，是用一小段 trace 觀察 SSD 寫入路徑如何運作，並從輸出統計理解 queue、FTL、NAND、GC 的關係。

建議照下面順序跑：

```text
建置程式
  |
  v
跑 sample trace
  |
  v
看統計輸出
  |
  v
改 config 或產生新 trace
  |
  v
比較結果差異
```

### DEMO 1：使用內建 sample trace

指令：

```bash
make
./ssd_fw_sim traces/sample.trace
```

目的：

- 用最小成本確認模擬器能讀取 trace 並跑完整條 write path。

原因：

- `traces/sample.trace` 已經放在專案內，不需要另外產生 workload。
- 適合第一次確認程式是否能正常執行。

`traces/sample.trace` 內容類似：

```text
WRITE 0 4
WRITE 8 4
WRITE 16 8
WRITE 0 2
```

這代表 host 依序送出多筆 write request。`WRITE 0 2` 會覆寫前面寫過的 LPN 0、1，因此可以觀察 out-of-place update 造成舊 page invalid 的效果。

### DEMO 1 的執行流程圖

```text
./ssd_fw_sim traces/sample.trace
  |
  v
main.c 讀取 trace
  |
  v
nvme_submit_write()
  |
  v
nvme_issue_pending()
  |
  v
scheduler_run()
  |
  v
ftl_handle_request()
  |
  v
nand_program_page()
  |
  v
nvme_post_completion()
  |
  v
stats_print()
```

### DEMO 1 要看哪些輸出

執行後會先看到 SSD 設定：

```text
=== SSD Configuration ===
total_blocks           : 128
pages_per_block        : 64
logical_pages          : 4096
request_queue_depth    : 256
gc_free_block_threshold: 8
```

這段代表目前模擬的 SSD 幾何與延遲設定。

接著會看到統計：

```text
=== SSD Statistics ===
Host Requests          : 7
Host Pages             : 46
NAND Writes            : 46
NAND Reads             : 0
NAND Erases            : 0
GC Count               : 0
Write Amplification    : 1.00
```

讀法：

| 輸出 | 代表什麼 | 為什麼重要 |
|------|----------|------------|
| `Host Requests` | trace 中成功處理的 request 數 | 用來確認輸入 workload 規模 |
| `Host Pages` | host 要求寫入的 page 總數 | WA 的分母 |
| `NAND Writes` | 實際 NAND program 次數 | WA 的分子，包含 GC 搬移 |
| `NAND Reads` | GC 搬移 valid page 時的 read 次數 | 沒有 GC 時通常是 0 |
| `NAND Erases` | block erase 次數 | 代表是否發生 GC |
| `GC Count` | GC 成功執行次數 | 用來觀察空間壓力 |
| `Write Amplification` | 實際寫入量 / host 寫入量 | 衡量 GC 與搬移成本 |

如果 `GC Count = 0`、`Write Amplification = 1.00`，代表 sample trace 還沒有把空間壓到需要 GC，因此 NAND 實際寫入量等於 host 寫入量。

### DEMO 2：輸出 CSV 方便比較

指令：

```bash
./ssd_fw_sim --csv stats.csv traces/sample.trace
```

目的：

- 將統計結果輸出成 `stats.csv`，方便用試算表或腳本比較不同 workload。

原因：

- 終端機輸出適合快速閱讀。
- CSV 適合做多組實驗比較，例如比較不同 `gc_free_block_threshold` 對 WA 的影響。

關鍵字：

| 關鍵字 | 英文 | 涵義 |
|--------|------|------|
| CSV | Comma-Separated Values | 以逗號分隔欄位的文字表格格式 |
| Metric | Metric | 可量化的觀察指標，例如 WA、latency、GC count |
| Workload | Workload | 一組輸入請求，這裡就是 trace 檔 |

### DEMO 3：使用自訂設定檔

指令：

```bash
./ssd_fw_sim --config ssd.conf traces/sample.trace
```

目的：

- 用 `ssd.conf` 覆寫預設 SSD 幾何與延遲。

原因：

- 不同 SSD 容量、block/page 配置、GC 門檻會影響統計結果。
- 將設定放在檔案中，比直接改程式碼更容易重複實驗。

建議觀察：

| 想觀察的行為 | 可調整的 key | 預期影響 |
|--------------|--------------|----------|
| 讓 GC 更容易出現 | 降低 `total_blocks` 或提高寫入量 | free block 較快不足 |
| 提早做背景 GC | 提高 `gc_free_block_threshold` | foreground GC 機率下降，但 GC 可能更早發生 |
| 放大 erase 對延遲的影響 | 提高 `erase_latency_us` | GC 發生時 total latency 會更明顯 |
| 模擬 host 較慢送 request | 提高 `trace_inter_arrival_us` | queue latency 可能下降 |

注意：

- `logical_pages` 不可大於 `total_blocks * pages_per_block`。
- `gc_free_block_threshold` 必須介於 1 到 `total_blocks - 1`。
- value 只能寫數字，例如 `program_latency_us=200`，不要寫 `200us`。

### DEMO 4：產生自己的 trace

指令：

```bash
python3 scripts/gen_trace.py --mode mixed --count 100 --max-lba 4096 --max-size 8 --output traces/demo.trace
./ssd_fw_sim traces/demo.trace
```

目的：

- 產生新的 workload，觀察不同寫入型態對 SSD 統計的影響。

原因：

- Sequential write 和 random write 對 FTL 與 GC 的壓力不同。
- 自訂 trace 可以讓你做可重複的實驗，而不是只看固定 sample。

模式說明：

| 模式 | 英文 | 行為 |
|------|------|------|
| `sequential` | Sequential Workload | LBA 連續往後寫，接近循序寫入 |
| `random` | Random Workload | 每筆 request 隨機選 LBA，容易造成覆寫與 invalid page |
| `mixed` | Mixed Workload | 混合循序與隨機寫入，較接近一般使用情境 |

新手建議：

- 先用 `--count 100` 或 `--count 1000`，避免輸出太大不易觀察。
- 若想更容易看到 GC，可搭配較小的設定檔，例如降低 `total_blocks`。

### DEMO 5：完整建置與驗證流程

如果要展示整個專案從乾淨狀態到可執行、可驗證、可輸出結果，可照這個順序：

```bash
make clean
make
make test
./ssd_fw_sim --config ssd.conf --csv stats.csv traces/sample.trace
```

每一步的目的：

| 步驟 | 目的 |
|------|------|
| `make clean` | 清掉舊建置結果，確保接下來不是吃到舊檔 |
| `make` | 建置主程式 |
| `make test` | 驗證核心模組沒有壞掉 |
| `./ssd_fw_sim ...` | 跑 DEMO workload 並輸出統計 |

這條流程適合用來確認「程式能從零建置、測試能通過、範例能執行、結果能保存」。

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
