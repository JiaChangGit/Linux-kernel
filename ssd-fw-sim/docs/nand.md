# NAND 筆記 (NAND Model Notes)

本專案的 NAND model 不保存真實 payload，只保存 page state、logical page number 和 block counter。它的目標是讓 FTL 和 GC 的狀態轉移可以被測試。

## Page 狀態機

```text
FREE
  -> nand_program_page()
VALID
  -> nand_invalidate_page()
INVALID
  -> nand_erase_block()
FREE
```

| 狀態 | English | 意義 |
| --- | --- | --- |
| `NAND_PAGE_FREE` | Free Page | 尚未被寫入，可 program。 |
| `NAND_PAGE_VALID` | Valid Page | 保存某個 LPN 的最新或仍需保留資料。 |
| `NAND_PAGE_INVALID` | Invalid Page | 已被新版本取代，可等 GC 回收。 |

## 關鍵 API

| API | 作用 | 類似 API | 選擇理由 |
| --- | --- | --- | --- |
| `nand_allocate_page()` | 從目前 write block 取得下一個 page。 | 在 `nand_program_page()` 內自動配置 | 分開 allocate 和 program，方便測試空間不足與 GC。 |
| `nand_program_page()` | 將 FREE page 改成 VALID，記錄 LPN。 | 直接改 `page->state` | API 會同步更新 block 的 valid/free counters。 |
| `nand_invalidate_page()` | 將 VALID page 改成 INVALID。 | 直接把 mapping 改掉不管 NAND | NAND 狀態也要更新，GC 才知道哪些 page 可回收。 |
| `nand_erase_block()` | 將整個 block reset 為 FREE，erase count 加一。 | page-level erase | NAND erase granularity 是 block，不是 page。 |

## NAND 和一般記憶體的差別

| 比較 | DRAM / 一般記憶體 | NAND Flash |
| --- | --- | --- |
| 覆寫 | 可以原地寫入。 | 不適合原地覆寫，通常要寫到新 page。 |
| 抹除 | 不需要 erase 才能重寫。 | 要以 block 為單位 erase。 |
| 寿命 | 不用在此模型追蹤。 | 真實 SSD 需要考慮 erase count 和 wear leveling。 |

## 目前限制

- 沒有 payload，因此無法驗證「讀回資料內容」。
- `nand_program_page()` 沒有檢查原 page 是否一定為 FREE；目前依賴 FTL 和 allocator 的流程保證。
- `erase_count` 有記錄，但目前沒有用在 wear leveling policy。
