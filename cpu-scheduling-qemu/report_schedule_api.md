# CPU Scheduling Simulator (QEMU Environment) API 技術報告

本報告針對 `/cpu-scheduling-qemu` 子專案進行深度 Codebase Trace 與架構分析。內容完全基於實體原始碼 (`scheduler.c`) 及自動化部署腳本的實際實作。

---

## 第一階段：Codebase Trace (程式碼追蹤)

### 1. Project Structure (專案結構)

- **Source Files**:
    - `src/scheduler.c`: 核心模擬器實作，包含五種調度演算法 (FCFS, SJF, SRTF, Priority, RR)。
- **Header Files**:
    - 無專屬 `.h` 檔案，所有定義均包含於 `scheduler.c` 中。
- **Workload Files**:
    - `src/workload_demo.txt`: 用於功能展示的任務列表。
    - `src/workload_bench.txt`: 用於效能基準測試的較大規模任務列表。
- **Build System**:
    - `scripts/01_setup_env.sh`: 在 Host 端使用 `gcc` 編譯 `scheduler.c` 並驗證，隨後透過 `base64` 編碼嵌入 VM 映像檔。
- **Scripts**:
    - `01_setup_env.sh`: 環境建置（QEMU + Cloud-init）。
    - `02_start_vm.sh`: 啟動模擬環境。
    - `03_demo.sh`: 執行展示腳本。
    - `04_benchmark.sh`: 執行所有演算法的效能對比。

### 2. Semantic Element Extraction (語義要素萃取)

- **API (Standard Library)**: `scanf`/`printf` (I/O), `qsort` (排序), `strcmp` (演算法選擇)。
- **Data Structures**:
    - `typedef struct Process`: 封裝任務屬性 (PID, Burst, Arrival, Priority) 及統計指標 (Start, Finish, Waiting, TAT)。
    - `typedef struct GanttSlot`: 紀錄單次 CPU 使用片段 (PID, Start, End)。
- **Execution Model**: **離散時間模擬 (Discrete-Time Simulation)**。不涉及真實多執行緒，而是透過全域 `clock` 變數模擬 CPU 時鐘。
- **Synchronization**: **目前程式碼中未觀察到**任何同步原語 (Mutex/Spinlock)，因為模擬器為單執行緒循序執行。
- **Memory Management**: 使用全域靜態陣列 (`proc[64]`, `gantt[12800]`)。

### 3. API / Algorithm Inventory

| 名稱 | 類型 | 呼叫位置 | 用途 | 影響 |
| :--- | :--- | :--- | :--- | :--- |
| `load_processes` | Function | `scheduler.c:266` | 從 `stdin` 讀取任務定義。 | 初始化全域 `proc` 陣列。 |
| `sched_fcfs` | Function | `scheduler.c:269` | 先來先服務調度。 | 依 `arrival` 排序後線性執行。 |
| `sched_sjf` | Function | `scheduler.c:271` | 短工作優先 (非搶佔)。 | 每次時鐘演進時選取最短工作。 |
| `sched_srtf` | Function | `scheduler.c:273` | 最短剩餘時間優先 (搶佔)。 | 每時鐘週期 (tick) 重新評估最優任務。 |
| `sched_rr` | Function | `scheduler.c:277` | 輪轉調度 (Round Robin)。 | 使用佇列 (Queue) 維護公平性。 |
| `gantt_push` | Function | `scheduler.c:54` | 紀錄甘特圖片段。 | 合併連續的相同 PID 片段以簡化輸出。 |

### 4. Call Graph (呼叫圖譜)

- **Initialization Chain**:
    `main` -> `load_processes` (由 `stdin` 獲取任務數與屬性)

- **Execution Chain**:
    `main` -> `sched_fcfs` | `sched_sjf` | `sched_srtf` | `sched_priority` | `sched_rr`
    各演算法內部循環呼叫 `gantt_push` 紀錄執行歷程。

- **Finalization Chain**:
    `sched_*` -> `compute_stats` (計算 TAT/Waiting)
    `sched_*` -> `print_results` (輸出表格與 BENCHMARK 標記)
    `sched_*` -> `print_gantt` (視覺化甘特圖)

### 5. Struct / Resource Tracing

- **`Process proc[MAX_PROC]`**:
    - **Ownership**: 全域靜態擁有。
    - **Lifetime**: 程式執行期間。
    - **State Transition**: `Arrival` (輸入) -> `Start` (選中) -> `Remaining` 遞減 (僅搶佔型) -> `Finish` (完工)。

- **`GanttSlot gantt[...]`**:
    - **Capacity**: `MAX_PROC * 200` = 12,800 個槽位。
    - **Risk**: 在 Round Robin 模式下，若 `time_quantum` 極小且任務極多，可能存在溢位風險。

### 6. Execution Trace (執行追蹤)

```text
[Loading Phase]
stdin -> load_processes() -> proc[] array initialized

[Simulation Phase (以 SRTF 為例)]
Loop while (completed < n_proc):
  1. Find min(proc[i].remaining) where arrival <= clock
  2. If found:
     a. If first time: set proc[i].start = clock
     b. gantt_push(pid, clock, clock+1)
     c. proc[i].remaining--
     d. If finished: set proc[i].finish = clock+1
  3. clock++

[Reporting Phase]
compute_stats() -> turnaround = finish - arrival -> print_results()
```

---

## 第二階段：Architecture / API Technical Report

### 1. Execution Semantics (執行語義)

本專案實作了一個**確定性調度模擬器 (Deterministic Scheduler Simulator)**。其架構核心在於將「調度邏輯」與「時間演進」解耦：

- **時間演進 (Time Progression)**：非搶佔式算法 (FCFS, SJF) 採用「跳躍式時間」，直接跳至任務完成點；搶佔式算法 (SRTF, RR) 則採用「步進式時間 (Tick-based)」，確保在每個最小單位時間點都能進行重調度。
- **任務選取 (Task Selection)**：各 `sched_*` 函式封裝了其專屬的選擇準則（Burst Time, Priority, 或 Queue Position）。

### 2. Callback & Dispatch 機制

- **排序回呼 (Comparison Callbacks)**：
    - 專案利用 C 標準庫 `qsort` 配合自定義回呼函式 (`cmp_arrival`, `cmp_burst`, `cmp_priority`)。
    - **技術決策分析**：這簡化了演算法初期的排序需求，例如 FCFS 只需一次 `qsort` 即可線性執行。但對於動態變化的算法 (SRTF)，系統選擇在循環內進行線性搜尋 (Linear Search) 而非維護優先權佇列 (Priority Queue)，在 `MAX_PROC=64` 的規模下是合理的效能取捨。

### 3. Resource Management & Ownership

- **靜態記憶體模型**：
    - 採用 `MAX_PROC` 限制 (64 任務)。這種設計確保了模擬器的記憶體足跡 (Memory Footprint) 是恆定的，且不涉及 `malloc`/`free` 的動態記憶體管理，消除了洩漏風險。
    - **Ownership**：所有演算法函式皆直接對全域 `proc` 陣列進行「破壞性修改 (Destructive Updates)」。

### 4. 演算法實作對比與分析

- **FCFS (First-Come, First-Served)**：
    - 差異：最簡實作，僅依賴 `cmp_arrival`。
    - 缺點：無法處理任務間隙，程式碼中透過 `if (clock < proc[i].arrival) clock = proc[i].arrival;` 手動校正時鐘。
- **SRTF (Preemptive SJF)**：
    - 差異：是本專案中複雜度最高的演算法。它模擬了時鐘中斷 (Clock Interrupt)，每單位時間 `clock++` 後即重新搜尋最短剩餘任務。
- **Round Robin (RR)**：
    - 差異：唯一引入了「佇列 (Queue)」概念的實作。其佇列大小設為 `MAX_PROC * 200`，反映了任務可能反覆進出佇列的特性。

### 5. Data Flow & State Transition

任務的狀態轉換由演算法函式內部邏輯驅動：
1.  **Entry**：`load_processes` 將任務狀態設為待命。
2.  **Activation**：當 `arrival <= clock` 且被演算法選中，寫入 `proc[i].start`。
3.  **Progression**：對於搶佔式算法，`proc[i].remaining` 隨著 `clock` 演進遞減。
4.  **Termination**：當 `remaining == 0` 或執行完畢，寫入 `proc[i].finish`。

### 6. Potential Bugs & Optimization (靜態分析建議)

- **甘特圖溢位**：`gantt[MAX_PROC * 200]` 在極端 Round Robin 參數下（如總 Burst=10000, Quantum=1）存在溢位風險。建議增加邊界檢查。
- **時鐘前進效率**：SRTF 在無任務準備好時使用 `clock++` 逐秒空轉，若任務到達時間跨度極大，效能會顯著下降。可參考 `sched_sjf` 的做法直接跳至下一個 `arrival`。
- **資料類型**：統計平均值使用 `double` 輸出至四位小數 (`%.4f`)，足以支援精確的效能對比。

---
**結論**：`/cpu-scheduling-qemu` 的模擬器設計精簡且高效，透過靜態資源配置與標準化輸出，成功建立了一個易於在 QEMU 自動化環境中驗證的調度分析平台。

