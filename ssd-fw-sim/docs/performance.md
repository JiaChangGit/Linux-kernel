# 效能指標筆記 (Performance Metrics)

本專案的效能數字用來比較不同 trace 或設定檔，不應解讀成真實 SSD 的效能。

## 主要指標

| 指標 | English | 計算方式 | 用途 |
| --- | --- | --- | --- |
| 寫入放大 | Write Amplification, WA | `nand_write_count / host_page_count` | 觀察 GC 搬移 valid page 對 NAND 寫入量的影響。 |
| 佇列延遲 | Queue Latency | `dispatch_time - submit_time` | 觀察 request 在韌體佇列中等待多久。 |
| 服務延遲 | Service Latency | `completion_time - dispatch_time` | 觀察 FTL、NAND program、GC 花多久。 |
| 總延遲 | Total Latency | `completion_time - submit_time` | request 從 host submit 到 completion 的總時間。 |
| 最大延遲 | Max Latency | 所有 request 中最高 total latency | 用來觀察 GC 造成的 long-tail latency。 |

所有時間單位都是 microseconds (`us`)。

## API 與輸出

| API | 作用 | 類似方式 | 選擇理由 |
| --- | --- | --- | --- |
| `stats_update_request()` | 累加 queue/service/total latency。 | 每個模組各自印 log | 集中統計可避免公式不一致。 |
| `stats_print()` | 印出人類可讀 summary。 | 只輸出 CSV | 新手直接執行即可看結果。 |
| `stats_export_csv()` | 輸出 CSV 欄位。 | JSON / binary log | CSV 可直接用試算表或 shell script 比較。 |

## 解讀注意事項

- WA 高，不一定代表程式錯；可能是 trace 造成大量 overwrite，導致 GC 搬移 valid page。
- Queue latency 高，通常代表 request 等待前面工作完成。
- Service latency 高，常見原因是 foreground GC 或 valid page migration。
- 目前 CSV 欄位比 stdout summary 少，若要比較 queue/service latency，需要看 stdout 或擴充 CSV。
