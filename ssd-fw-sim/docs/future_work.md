# 後續可擴充方向 (Future Work)

以下都不是目前已完成的功能，而是適合在現有架構上延伸的方向。

| 方向 | English | 需要新增或調整的 API | 為什麼值得做 |
| --- | --- | --- | --- |
| 讀取路徑 | Read Path | `nvme_submit_read()`、`ftl_handle_read()`、`nand_read_page()` | 可以驗證 GC 搬移後 L2P 是否仍能讀到正確位置。 |
| TRIM / Deallocate | TRIM / Deallocate | `ftl_handle_trim()`、mapping invalidation API | 讓 host 主動告知哪些 LPN 不再需要，增加 invalid page，影響 GC。 |
| 磨耗均衡 | Wear Leveling | victim policy 加入 `erase_count` | 避免少數 block 被反覆 erase。 |
| 冷熱資料分離 | Hot/Cold Data Separation | `lpn_write_count` 分類、不同 write block pool | 降低 GC 搬移熱資料造成的 WA。 |
| 背景 GC 執行緒 | Background GC Thread | thread / event loop / locking | 目前 background GC 是同步流程；若改成 thread，才更接近真實韌體。 |
| 多通道 / 多 die | Parallel Channels / Dies | channel scheduler、per-channel NAND state | 可以研究平行度對 latency 的影響。 |
| NVMe admin command | NVMe Admin Command Simulation | identify、format、feature command path | 讓 NVMe 模型不只處理 WRITE。 |
| 掉電保護 | Power-loss Safe Checkpoint | metadata checkpoint、journal、replay | 目前沒有 power-loss recovery；加入後才能討論一致性恢復。 |
| OOB / metadata journal | Out-of-Band Metadata Journal | page metadata、journal record | 更貼近真實 NAND page 附帶 metadata 的設計。 |

## 延伸時的優先順序

1. 先做 READ path，因為它能驗證 L2P 與 GC migration 的正確性。
2. 再做 TRIM，讓 GC 有更真實的 invalid page 來源。
3. 接著做 wear leveling 或 hot/cold separation，開始比較不同 GC policy。
4. 最後再做 power-loss recovery，因為它需要較完整的 metadata 模型。
