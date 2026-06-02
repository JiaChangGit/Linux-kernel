# GC 筆記 (Garbage Collection Notes)

GC (Garbage Collection) 的目標是回收含有 invalid pages 的 block。因為 NAND 只能以 block 為單位 erase，所以 GC 必須先搬走 victim block 裡仍然有效的 page。

## 目前流程

```text
gc_run()
  -> gc_select_victim_block()
  -> gc_migrate_valid_pages()
       -> nand_allocate_page()
       -> nand_program_page()
       -> mapping_set_physical_page()
       -> nand_invalidate_page(old page)
  -> nand_erase_block(victim)
  -> free_block_pool_push(victim)
```

## 關鍵 API

| API | English | 作用 | 類似 API | 選擇理由 |
| --- | --- | --- | --- | --- |
| `gc_needed()` | GC Trigger Check | 判斷是否低於 `gc_free_block_threshold`。 | 每次掃描整顆 NAND | 只看 free block pool，成本低且容易理解。 |
| `gc_run(ftl, foreground)` | GC Execution | 執行 victim 選擇、搬移、erase。 | 把 GC 寫在 FTL write loop 裡 | 獨立 API 較容易測試，也能分開統計 foreground/background GC。 |
| `gc_select_victim_block()` | Victim Selection | 選 invalid page count 最高的 block。 | random victim / wear-aware victim | greedy policy 容易觀察 WA 與 migration 成本。 |
| `gc_migrate_valid_pages()` | Valid Page Migration | 搬移仍有效的 page 並更新 L2P。 | 直接 erase victim | 直接 erase 會遺失 valid data，必須先搬移。 |

## Victim Policy 比較

| 策略 | English | 優點 | 限制 |
| --- | --- | --- | --- |
| 貪婪策略 | Greedy Policy | 選 invalid page 最多的 block，搬移成本通常較低。 | 不考慮 erase count，可能造成磨耗不均。 |
| 成本效益策略 | Cost-Benefit Policy | 同時考慮 invalid 數量與資料冷熱。 | 需要更多 metadata。 |
| 磨耗感知策略 | Wear-aware Policy | 可降低單一 block 被過度 erase 的風險。 | 需要設計 wear leveling，程式複雜度較高。 |

## 困難與挑戰

- **L2P 更新**：valid page 搬到新 PPA 後，一定要更新 mapping；否則 victim erase 後，LPN 會指到錯的位置。
- **長請求與保留空間**：很長的 WRITE request 可能消耗掉 GC migration 所需的 free pages，因此 write loop 中會提早檢查 `gc_needed()`。
- **Foreground vs Background GC**：目前 background GC 不是獨立執行緒，而是 request path 內同步執行；文件不能把它描述成真正背景 thread。
