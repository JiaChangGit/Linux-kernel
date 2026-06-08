# FTL 筆記 (Flash Translation Layer Notes)

FTL (Flash Translation Layer) 的任務是把 host 看到的邏輯頁號 LPN (Logical Page Number) 轉成 NAND 內部的實體頁位置 PPA (Physical Page Address)。

目前程式只實作 WRITE path，沒有 READ、TRIM、wear leveling，也沒有 power-loss recovery。

## 寫入流程

單一 LPN 的實際順序對應 `src/ftl.c`：

1. `mapping_get_physical_page()`：查舊 PPA。
2. `gc_needed()`：若 free block pool 低於門檻，先嘗試 background GC。
3. `nand_allocate_page()`：配置新 PPA。
4. `nand_program_page()`：把新 page 設為 VALID。
5. `nand_invalidate_page()`：若有舊 PPA，將舊 page 標成 INVALID。
6. `mapping_set_physical_page()`：更新 L2P mapping，讓 LPN 指到新 PPA。

```text
WRITE LPN 10
  -> old PPA = mapping_get_physical_page(10)
  -> new PPA = nand_allocate_page()
  -> nand_program_page(new PPA, LPN 10)
  -> nand_invalidate_page(old PPA)
  -> mapping_set_physical_page(10, new PPA)
```

## Mapping 策略比較

| 策略 | English | 優點 | 限制 | 本專案選擇 |
| --- | --- | --- | --- | --- |
| 頁級映射 | Page-level Mapping | 每個 LPN 都能獨立更新，適合 random write。 | mapping table 較大。 | 採用，因為最容易示範 out-of-place update 與 GC migration。 |
| 區塊級映射 | Block-level Mapping | mapping table 小。 | 小寫入容易造成大量搬移。 | 未採用，會讓重點變成 merge policy。 |
| 混合映射 | Hybrid Mapping | 實務上常見，能折衷空間與效能。 | 程式複雜，需 log block / data block 管理。 | 後續可延伸。 |

## 重要限制

- `nand_program_page()` 目前沒有回傳失敗，因此這份模擬器無法展示 program failure recovery。
- 寫入順序不能說成 journaling。它只是保留 out-of-place update 的基本語意，並沒有 checkpoint、journal replay 或 power-loss recovery。
- 因為目前沒有 read path，測試主要檢查 mapping 和 page state 是否一致，而不是讀回 payload。
