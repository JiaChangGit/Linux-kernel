# CPU 排程模擬器技術報告

## 摘要

本專案表面上是 CPU 排程演算法模擬器，但若從 firmware／embedded systems engineering 的角度來看，它更像是一個「以有限資源模型、明確狀態轉移、可重現執行環境、可觀測執行軌跡」為核心的系統實驗平台。專案使用 C 語言實作 FCFS、SJF、SRTF、Priority Scheduling、Round Robin 等排程器，並以 QEMU、Ubuntu cloud image、cloud-init、Bash 自動化腳本建構出可重現的測試環境。其核心價值不只在演算法正確性，也在於展示 firmware 開發常見的幾項關鍵能力：資料結構設計、狀態機式控制流程（state-machine-like control flow）、時間行為分析（timing behavior analysis）、結構化 trace 輸出、以及自動化驗證流程。

更重要的是，本專案雖然不是直接撰寫 MCU bootloader、driver 或 RTOS kernel，但它所訓練的能力與 firmware 開發高度相關。以 `scheduler.c` 為例，程式必須在清楚定義的輸入格式下，維護 process 狀態、更新時間、記錄執行軌跡，並保證每種演算法在不同工作負載下都能輸出一致且可驗證的結果。這種「對時間、狀態、可觀測性極度敏感」的工程思維，正是 firmware skill 的核心。本報告因此不只解釋演算法，也會特別從 firmware 視角分析：這個專案實際鍛鍊了哪些能力、Trace 為何重要、以及這些設計如何對應到嵌入式系統開發中的真實工作。

## 1. 報告定位與專案範圍

本報告根據目前專案目錄中的實際內容撰寫，核心觀察對象包括：

- `src/scheduler.c`
- `src/workload_demo.txt`
- `src/workload_bench.txt`
- `scripts/01_setup_env.sh`
- `scripts/02_start_vm.sh`
- `scripts/03_demo.sh`
- `scripts/04_benchmark.sh`
- `scripts/05_cleanup.sh`
- `results/demo_output.txt`
- `results/benchmark.csv`
- `results/benchmark_report.txt`
- `Makefile`

這個專案本質上是「在 QEMU 虛擬機中執行的 CPU 排程演算法模擬器（CPU Scheduling Algorithm Simulator）」，不是直接修改 Linux kernel scheduler，也不是使用 Linux kernel tracepoint、`ftrace`、`perf`、`eBPF` 等核心級追蹤工具。這一點必須先說清楚，因為後面談到的 Trace，在本專案裡是「模擬器層級的執行軌跡（execution trace）」與「文字化排程時間線（textual scheduling timeline）」，而不是作業系統核心事件追蹤。

換句話說，本專案重點有三層：

1. 演算法層：FCFS、SJF、SRTF、Priority、Round Robin。
2. 系統驗證層：用 QEMU + Ubuntu 24.04 建立可重現的實驗環境。
3. 觀測層：用 Gantt Chart、每個 process 的統計欄位、以及 `BENCHMARK` 結構化輸出，形成可分析的 Trace。

---

## 2. 專案目標與整體技術構成

### 2.1 專案目標

此專案要解決的不是「真正接管 Linux CPU 排程」，而是要把作業系統課程中常見的 CPU scheduling 理論，轉成一個可以：

- 編譯
- 執行
- 觀察
- 重複實驗
- 比較不同演算法數據

的完整實驗平台。

### 2.2 用到的主要技術

本專案實際使用到的技術如下：

| 類別 | 技術 | 說明 |
|---|---|---|
| 程式語言 | C | 用於實作排程模擬器 `src/scheduler.c` |
| Shell 腳本 | Bash | 用於自動化 VM 建置、啟動、Demo、Benchmark、Cleanup |
| 虛擬化 | QEMU | 建立可重現的 Ubuntu 24.04 VM 環境 |
| VM 初始化 | cloud-init | 首次開機自動佈署 scheduler binary、source、workload |
| 編譯工具 | GCC | 在 host 端先做編譯驗證 |
| 自動化執行 | Makefile | 將 setup/start/demo/bench/clean 串成可重複流程 |
| 遠端連線 | SSH + sshpass | 由 host 端自動登入 VM 執行模擬器 |
| 數據輸出 | CSV | 將 benchmark 結果輸出到 `results/benchmark.csv` |
| 可視化輸出 | Gantt Chart | 用純文字方式顯示排程時間線 |

### 2.3 為什麼這樣的技術組合合理

這份專案的設計非常偏向教學與可重現性（reproducibility）：

- 用 C 實作演算法，接近作業系統課程的傳統風格。
- 用 QEMU 避免每台主機環境不同造成實驗偏差。
- 用 cloud-init 把 VM 首次啟動流程自動化，不需要手動安裝或手動複製檔案。
- 用 Bash 把整個 pipeline 串起來，從建立 VM 到產出 benchmark 都可重跑。
- 用純文字 Trace 與 CSV，讓結果既能人讀，也能機器後處理。

---

## 3. 專案架構與執行流程

### 3.1 從 Host 到 VM 的流程

整體流程可以拆成下面幾步：

1. `scripts/01_setup_env.sh`
   負責檢查依賴、下載 Ubuntu 24.04 cloud image、建立 qcow2 磁碟、產生 cloud-init seed ISO。
2. `scripts/02_start_vm.sh`
   負責啟動 QEMU，並等待 SSH 可連線與 cloud-init 完成。
3. `scripts/03_demo.sh`
   在 VM 內執行 demo workload，輸出完整統計與 Gantt Chart。
4. `scripts/04_benchmark.sh`
   在 VM 內執行 benchmark workload，抽取平均等待時間、平均週轉時間、平均回應時間，輸出成 CSV 與報告。
5. `scripts/05_cleanup.sh`
   關閉 VM 並清理產生的 VM 檔案。

### 3.2 Makefile 的角色

`Makefile` 將這些步驟整理成標準目標：

- `make build`
- `make demo-host`
- `make setup`
- `make start`
- `make demo`
- `make bench`
- `make clean`
- `make clean-full`
- `make all`

這表示本專案不只是一個單一 C 程式，而是一個完整的可操作實驗框架（experiment framework）。

### 3.3 QEMU 與 Ubuntu Cloud Image

本專案選擇 `x86_64` + Ubuntu 24.04 cloud image，有幾個實際優勢：

- 與一般 x86 主機相容。
- 不需要交叉編譯（cross-compilation）。
- cloud image 已內建 cloud-init 支援。
- 適合自動佈署。

這裡使用的不是一般桌面安裝 ISO，而是 cloud image。兩者差異很重要：

- 安裝 ISO 偏向人工安裝流程。
- cloud image 偏向自動化佈署與初始化。

對教學型專案而言，cloud image 更適合，因為可以把環境建置流程腳本化。

---

## 3.4 從 Firmware Skill 角度看這個專案

如果只把這份專案看成作業系統課的 scheduler demo，會低估它的工程價值。從 firmware skill 的角度，它至少對應到以下幾類能力：

1. 狀態管理（State Management）
   例如 `remaining`、`start`、`finish`、`done[]`、`started[]`、`in_queue[]` 都是在維護系統狀態。Firmware 工程師在寫 driver、ISR、protocol stack、RTOS task control block 時，也是在做同樣的事。

2. 時序推進（Time Progression）
   `clock` 的更新代表系統時間模型。這與 firmware 中處理 timer tick、watchdog window、polling interval、deadline 的思維相通。

3. 決策邏輯可驗證（Deterministic Decision Logic）
   演算法的選擇條件是可明確推導、可重播、可測試的。這和 firmware 中設計 deterministic behavior 十分接近，尤其是在 safety-critical 或 real-time 系統。

4. 可觀測性（Observability）
   `GanttSlot`、`print_gantt()`、`BENCHMARK` 輸出，對應到 firmware 開發裡常見的 log、trace buffer、UART debug message、event record。

5. 自動化驗證（Automated Validation）
   QEMU + script + benchmark 的組合，本質上就是 firmware 常見的自動化 test harness。差別只在本專案模擬的是 scheduler，而不是 peripheral、bus protocol 或 boot flow。

因此，雖然專案執行在 Ubuntu VM 上，而不是 bare-metal MCU，上述能力仍然非常貼近 firmware engineering 的核心素養。

---

## 4. `scheduler.c` 的資料結構設計

### 4.1 Process 結構

`src/scheduler.c` 使用 `Process` 結構來表示每個 process：

| 欄位 | 中文 | 英文 | 說明 |
|---|---|---|---|
| `pid` | 行程編號 | Process ID | 唯一識別一個 process |
| `arrival` | 到達時間 | Arrival Time | process 何時進入系統 |
| `burst` | 執行時間 | CPU Burst Time | process 需要多少 CPU 時間 |
| `remaining` | 剩餘執行時間 | Remaining Time | 主要用於可搶先演算法 |
| `priority` | 優先權 | Priority | 數字越小代表優先權越高 |
| `start` | 首次開始時間 | First Start Time | 第一次真正拿到 CPU 的時間 |
| `finish` | 完成時間 | Finish Time / Completion Time | process 執行完畢的時間 |
| `waiting` | 等待時間 | Waiting Time | 在 ready queue 等待 CPU 的總時間 |
| `turnaround` | 週轉時間 | Turnaround Time | 從 arrival 到 finish 的總時間 |
| `response` | 回應時間 | Response Time | 從 arrival 到首次拿到 CPU 的延遲 |
| `responded` | 是否回應過 | Responded Flag | 程式中已宣告，但實際流程主要以 `start == -1` 判斷首次執行 |

### 4.2 GanttSlot 結構

Trace 相關最重要的結構不是 `Process`，而是：

| 欄位 | 中文 | 英文 | 說明 |
|---|---|---|---|
| `pid` | 行程編號 | Process ID | 這段時間是誰在執行 |
| `start` | 片段起點 | Start Timestamp | 該段執行開始時間 |
| `end` | 片段終點 | End Timestamp | 該段執行結束時間 |

這個 `GanttSlot` 可以理解成「一段連續 CPU 使用區間（continuous CPU occupancy interval）」。

如果把每次排程決策都當成一個事件（event），那麼 `GanttSlot` 就是把多個事件壓縮後的區間表示法。

### 4.3 為什麼 `GanttSlot` 很重要

很多學生寫排程模擬器時，只關注最後的平均時間，但看不到「中間到底怎麼排」。本專案比較好的地方在於，它不只算結果，還保留了「時間線」。

這會帶來三個直接好處：

1. 可以驗證演算法是否真的照理論執行。
2. 可以把 preemption 是否發生看得非常清楚。
3. 可以作為 Trace 的基礎資料結構。

---

## 5. 核心統計公式與其意義

### 5.1 三個核心指標

本專案統計三個最重要的 scheduling metrics：

| 縮寫 | 中文 | 英文 | 公式 |
|---|---|---|---|
| AWT | 平均等待時間 | Average Waiting Time | 平均 `waiting` |
| ATT | 平均週轉時間 | Average Turnaround Time | 平均 `turnaround` |
| ART | 平均回應時間 | Average Response Time | 平均 `response` |

### 5.2 單一 process 的公式

程式中的計算方式非常明確：

```text
turnaround = finish - arrival
waiting    = turnaround - burst
response   = start - arrival
```

這三條公式非常關鍵，因為它們定義了整份 benchmark 的數值來源。

### 5.3 這些指標分別在描述什麼

`Waiting Time` 描述的是 process 在 ready queue 裡「乾等 CPU」的時間。  
`Turnaround Time` 描述的是 process 從進來到完成的完整生命週期。  
`Response Time` 描述的是使用者或系統何時第一次感受到「這個 process 有被服務」。  

這三者不是同一件事，不能混為一談。

例如：

- 一個 process 很快就第一次拿到 CPU，`response time` 可以很好。
- 但它之後可能被多次中斷，最後完成得很晚，`turnaround time` 仍可能很差。

Round Robin 就常常會出現這種現象。

---

## 6. 各排程演算法的理論與實作對照

## 6.1 FCFS（First-Come First-Served，先來先服務）

### 理論

FCFS 按 arrival time 排序，誰先來誰先跑，不可搶先（non-preemptive）。

### 程式實作重點

- 先以 `qsort` 配合 `cmp_arrival` 按 arrival 排序。
- `clock` 若小於 process 的 arrival，就直接跳到 arrival。
- process 一旦開始執行，就跑完整個 burst。

### 技術特性

- 實作最簡單。
- 容易驗證正確性。
- 容易出現護送效應（Convoy Effect）。

### Convoy Effect（護送效應）

關鍵字：

- 護送效應：Convoy Effect

定義是：一個很長的工作如果排在前面，後面很多短工作都會被拖慢，好像整隊車被慢車堵住。

在 `results/demo_output.txt` 的 demo workload 中，`P1` burst 為 8，先執行到 `t=8`，導致後面明明更短的 `P2`、`P5`、`P6` 都必須排隊等待。這正是 FCFS 在教學上最典型的弱點。

---

## 6.2 SJF（Shortest Job First，最短工作優先）

### 理論

SJF 在 CPU 空出來時，從 ready processes 中選 burst 最短者執行。此專案實作的是不可搶先版本。

### 程式實作重點

- 使用 `done[]` 標記哪些 process 已完成。
- 每次在 `arrival <= clock` 且未完成的 process 中，找 `burst` 最小者。
- 若當前時刻沒有任何 ready process，就把 `clock` 推進到下一個 arrival。

### 為什麼平均等待時間通常會比較好

短工作先做，能讓更多工作提早離開系統，因此平均等待時間常會下降。

### 風險：Starvation（飢餓）

關鍵字：

- 飢餓：Starvation

若一直有更短的新工作加入，長工作就可能被持續延後。雖然本專案的 workload 是有限集合，最後一定會做完，但在真實系統或無限工作流中，SJF 類型演算法確實可能產生 starvation 問題。

---

## 6.3 SRTF（Shortest Remaining Time First，最短剩餘時間優先）

### 理論

SRTF 是 SJF 的可搶先版本（preemptive version）。  
每一個時間點都重新檢查 ready set，誰的 remaining time 最短，就讓誰執行。

### 程式實作重點

這份專案中的 SRTF 實作非常值得仔細看，因為它是 Trace 最明顯的來源之一：

1. 所有 process 初始化 `remaining = burst`。
2. `clock` 以「每次加 1」的方式前進。
3. 每一個 time unit 都重新掃描所有已到達且尚未完成的 process。
4. 選出 `remaining` 最小者。
5. 把該 process 執行 1 個時間單位。
6. 若剩餘時間歸零，記錄 `finish`。

### 這個實作代表什麼

它的時間粒度（time granularity）是「1 個時間單位」。  
也就是說，這不是事件驅動模擬（event-driven simulation），而是逐 tick 模擬（tick-by-tick simulation）。

這個設計的好處是：

- 容易理解
- 容易驗證
- 容易生成 Trace

代價是：

- 時間複雜度較高
- 若 workload 很大，效率不如 heap 或 event queue 實作

### SRTF 的 Trace 例子

以 demo workload 為例：

- `t=0`，只有 `P1` 到達，所以先跑 `P1`
- `t=1`，`P2` 到達，`P1` 剩餘 7，`P2` burst 4，因此搶先 `P1`
- `t=5`，`P2` 完成，此時 `P6` 已到達且 burst 最短，所以先跑 `P6`
- 接著 `P5`、`P4`、`P1`、`P3` 依 remaining time 決定順序

對應到 demo 輸出的 Gantt Chart：

```text
| P1 | P2 | P6 | P5 | P4 | P1 | P3 |
0    1    5    6    8    13   20   29
```

這條時間線不是憑空印出來的，它來自每個 time unit 都呼叫 `gantt_push()` 所累積出的執行片段。

---

## 6.4 Priority Scheduling（優先權排程）

### 理論

此專案採用不可搶先 Priority Scheduling，且規則為：

- 數字越小，priority 越高
- 在 ready processes 中，優先選 priority 最小者
- 若 priority 相同，以較早 arrival 者優先

### 程式實作重點

- 與 SJF 類似，也使用 `done[]`。
- 差別在比較條件是 `priority`，不是 `burst`。

### 重要概念：Priority 不代表一定比較快完成

很多初學者會誤解「高優先權 = 總時間最短」。這是不正確的。

Priority Scheduling 最主要是在表達「系統偏好（system preference）」而不是「最佳平均時間」。  
如果高優先權 process 很長，它仍然可能讓低優先權 process 等很久。

### 可能問題：Priority Starvation

若沒有 Aging（老化機制），低優先權 process 可能長時間被壓住。  
本專案沒有實作 aging，因此報告中應明白指出：這是一個教學型簡化版本。

---

## 6.5 Round Robin（輪轉排程）

### 理論

Round Robin 使用固定時間片：

- 中文：時間片 / 時間量子
- 英文：Time Quantum

每個 process 最多跑 `Q` 個時間單位。若未完成，就回到 ready queue 尾端等待下一輪。

### 為什麼 Round Robin 常用於互動式系統

Round Robin 的核心優勢不是讓總完成時間最短，而是讓「每個人都很快先輪到一次」，因此 response time 常會很好。

### 程式實作資料結構

Round Robin 在本專案裡用了幾個關鍵陣列：

| 陣列 | 作用 |
|---|---|
| `remaining[]` | 記錄各 process 尚未完成的 CPU 時間 |
| `started[]` | 紀錄 process 是否曾執行過 |
| `in_queue[]` | 紀錄 process 是否已被放進 queue |
| `queue[]` | ready queue，本質上是陣列版 FIFO |

### 實作流程細節

Round Robin 的流程不是只有「取出、執行、放回去」這麼簡單。這份實作有一個很重要的細節：

> 新到達的 process 會先入隊，再決定是否把目前尚未完成的 process 重新放回隊尾。

這個順序非常關鍵，因為它影響公平性（fairness）與首次反應速度（first response latency）。

如果順序反過來，當前 process 可能在某些情況下過度佔據 queue 前段，讓新來的 process 更晚被看到。

### RR 與 Q 值的本質

Q 太小：

- 好處：response time 很好
- 壞處：切換更頻繁，理論上 context switch overhead 會增加

Q 太大：

- 好處：切換較少
- 壞處：越來越像 FCFS

### 但要注意本專案「沒有明算 context switch cost」

這是一個必須精確說明的地方。

本專案的 `scheduler.c` 並沒有把 context switch overhead 額外加進時間模型，也沒有模擬 cache pollution、TLB flush、scheduler dispatch cost。  
所以在這份 simulator 裡，Round Robin 的「小 Q 代價」主要是排程順序造成的等待效果，而不是來自真正被加總進去的切換成本。

這表示：

- 報告可以談理論上的 context-switch tradeoff。
- 但不能誤寫成「本專案已實測 context switch 開銷」。

這兩件事完全不同。

---

## 7. Trace 的定義、角色與實作細節

這一節是本報告最重要的部分之一。

## 7.1 先澄清：本專案的 Trace 是什麼

在這個專案裡，Trace 不是 Linux kernel tracepoint，也不是 `perf record` 或 `ftrace` 收集到的底層排程事件。

本專案的 Trace 可以拆成三種層次：

1. `GanttSlot` 累積出的執行區段
2. `print_gantt()` 生成的文字化時間線
3. `print_results()` 輸出的 `BENCHMARK ...` 結構化結果列

所以如果用比較精確的說法，本專案做的是：

- Scheduling Trace（排程軌跡）
- Execution Trace（執行軌跡）
- Structured Telemetry Output（結構化遙測輸出）

而不是 kernel-level tracing。

## 7.2 `gantt_push()`：Trace 壓縮的核心

Trace 功能最關鍵的函式是：

- `gantt_push(int pid, int t_start, int t_end)`

它的功能不是單純 append，而是「合併連續相同 PID 的片段」。

邏輯是：

1. 如果目前 `gantt` 已經有資料
2. 且最新一筆的 `pid` 與現在要加入的 `pid` 相同
3. 那就直接把最新一筆的 `end` 改成新的 `t_end`
4. 否則才真的新增一個新的 `GanttSlot`

### 為什麼這個合併重要

以 SRTF 為例，程式每次只執行 1 個 time unit。  
假設某個 process 連跑 4 個 time unit，如果不合併，Trace 會變成：

```text
(P2,1,2), (P2,2,3), (P2,3,4), (P2,4,5)
```

這樣雖然正確，但可讀性很差。

有了合併後，就會變成：

```text
(P2,1,5)
```

這就是為什麼 demo 中 SRTF 的 Gantt Chart 雖然底層是逐 tick 決策，但輸出看起來仍然乾淨。

### 這種設計的工程意義

這其實是一種簡單的 trace compression（追蹤壓縮）或 run-length style consolidation（連續區段合併）。

優點：

- 節省輸出空間
- 提高可讀性
- 更接近人類理解排程時間線的方式

缺點：

- 若之後想做更細的事件分析，例如每 tick 的搶先原因，就還需要另一層更細粒度事件紀錄

換句話說，現在的 `gantt` 是「區間級 Trace」，不是「原因級 Trace」。

## 7.3 Trace 在各演算法中是如何被產生的

### FCFS / SJF / Priority

這三者都是不可搶先演算法，因此每個 process 通常只會產生一個主要區段。

例如 FCFS：

```text
P1: 0 -> 8
P2: 8 -> 12
P3: 12 -> 21
...
```

每當選定一個 process，程式就直接：

1. 設定 `start`
2. 設定 `finish`
3. 呼叫 `gantt_push(pid, start, finish)`

這類 Trace 結構簡單，幾乎一筆 slot 對應一個 process。

### SRTF

SRTF 是最能展現 Trace 價值的演算法。

它每一個 time unit 都可能改選 process，因此會頻繁呼叫：

```text
gantt_push(pid, clock, clock + 1)
```

如果 process 被連續選中，就由 `gantt_push()` 合併。  
如果中途被更短的工作搶走 CPU，就自然切成多段。

因此 SRTF 的 Trace 會直接揭露：

- 哪裡發生 preemption
- 哪個 process 被切斷
- 哪個新 process 插入

### Round Robin

Round Robin 的 Trace 不是每 1 單位，而是每次 time quantum 執行長度：

```text
run = min(remaining, quantum)
gantt_push(pid, clock, clock + run)
```

因此 RR 的 Trace 粒度是「一個 quantum 或剩餘尾段」。

若 `Q=1`，RR 的 Trace 幾乎會變成大量短片段。  
若 `Q=8`，Trace 就會更接近 FCFS 的長片段。

這也是為什麼 demo output 裡：

- `RoundRobin_Q1` 的 Gantt Chart 很碎
- `RoundRobin_Q8` 的 Gantt Chart 很像 FCFS

## 7.4 `print_gantt()`：Trace 的展示層

Trace 被記下來之後，還要能展示。  
`print_gantt()` 的任務就是把 `gantt[]` 轉成文字圖。

它做了兩件事：

1. 第一行印出 PID 區塊
2. 第二行印出時間節點

例如：

```text
| P1 | P2 | P6 | P5 | P4 | P1 | P3 |
0    1    5    6    8    13   20   29
```

這個輸出形式雖然簡單，但在教學上非常有效，因為它同時保留：

- 執行順序
- 區段長度
- preemption 切點
- 完成時刻

## 7.5 `BENCHMARK` 行：另一種 Trace

`print_results()` 裡除了印表格，還會印出一行：

```text
BENCHMARK <label> AWT=<...> ATT=<...> ART=<...>
```

這一行非常重要，因為它不是給人看漂亮格式而已，它是給腳本穩定解析的 machine-readable interface。

可以把它視為一種結構化事件輸出：

- 事件名稱：`BENCHMARK`
- 維度：演算法 label
- 欄位：AWT / ATT / ART

這種設計在工程上很成熟，因為：

- 人類可閱讀
- 腳本可解析
- 不依賴表格欄位寬度
- 不怕顏色碼或裝飾字元影響

## 7.6 `04_benchmark.sh` 如何消費這份 Trace

`scripts/04_benchmark.sh` 的流程如下：

1. SSH 到 VM 執行 scheduler
2. 擷取整份原始輸出到 `raw`
3. 用 `grep '^BENCHMARK'` 抓出結構化結果行
4. 用 `awk` 取出 label
5. 用 `grep -oP` 擷取 `AWT`、`ATT`、`ART`
6. 寫入 `results/benchmark.csv`

這代表 C 程式輸出的 structured trace，最後被 shell 腳本轉成 CSV 資料管線。

這是一條完整的資料流：

```text
scheduler.c
  -> BENCHMARK line
  -> 04_benchmark.sh parser
  -> benchmark.csv
  -> benchmark_report.txt
```

如果用系統設計語言來說，這就是一個輕量級 observability path（可觀測資料路徑）。

## 7.7 本專案的 Trace 做到了什麼，還沒做到什麼

### 已做到的部分

- 可以知道誰在什麼時間執行
- 可以知道何時切換 process
- 可以知道每個 process 的開始與完成時間
- 可以把平均指標結構化輸出，便於 benchmark 比較

### 尚未做到的部分

- 沒有記錄每次 preemption 的原因
- 沒有記錄 ready queue 當下內容
- 沒有記錄 context switch 次數
- 沒有記錄 idle CPU 區段為獨立 slot
- 沒有使用真正的 kernel tracing 框架

這些限制不代表設計不好，而是代表它是一個「教學導向、演算法導向」的 trace 系統，而不是 production-grade scheduler observability system。

---

## 7.8 為什麼 Trace 對 Firmware 特別重要

在一般應用程式開發中，錯誤有時可以靠 debugger、stack trace、例外訊息快速定位；但在 firmware 或 embedded 系統中，問題常常更難抓，因為：

- 系統可能沒有完整作業系統
- 錯誤可能只在特定時序下出現
- 出錯當下未必有互動式除錯器
- 一旦 reset，現場資訊可能消失

因此 Trace 幾乎是 firmware 工程中的核心技能之一。

### Trace

- 中文：追蹤、執行軌跡、事件軌跡
- 英文：Trace, Execution Trace, Event Trace

在 firmware 世界裡，Trace 常被用來回答幾個關鍵問題：

1. 當下誰先執行了？
2. 事件先後順序是什麼？
3. 中斷或任務切換發生在哪個時間點？
4. 某個 timeout 為什麼發生？
5. 哪個 state transition 不符合預期？

本專案中的 `gantt[]` 正是這種思維的簡化版。它雖然不是硬體 trace macrocell，也不是 ETM（Embedded Trace Macrocell），但它扮演的角色非常類似：把「程式實際怎麼跑」留下來。

### 對 firmware 工程的對應關係

| 本專案元件 | Firmware 對應概念 |
|---|---|
| `clock` | system tick / timer counter |
| `gantt[]` | trace buffer / event buffer |
| `gantt_push()` | event commit / trace record append |
| `print_gantt()` | debug dump / offline trace decode |
| `BENCHMARK` line | machine-readable diagnostics / CI telemetry |

### 一個 firmware 工程師會特別在意的點

不是只有「結果對不對」，而是：

- 系統如何到達這個結果
- 能不能重播
- 能不能在沒有互動式 debugger 的情況下還原現場

這也正是本專案 Trace 機制的真正教學價值。

---

## 8. Workload 格式與教學意義

### 8.1 輸入格式

`load_processes()` 讀取格式如下：

```text
<n>
<pid> <arrival> <burst> <priority>
...
```

例如 demo workload：

```text
6
1 0 8 3
2 1 4 1
3 2 9 4
4 3 5 2
5 4 2 5
6 5 1 3
```

### 8.2 為什麼 workload 外部化很重要

把 workload 放在 `src/workload_demo.txt` 和 `src/workload_bench.txt`，而不是寫死在程式裡，有三個優點：

1. 改 workload 不需要重新改演算法邏輯
2. 可以用不同輸入刻意製造特定現象
3. 便於 benchmark 重現

例如：

- 若想觀察 Convoy Effect，就安排早到的大 burst 工作
- 若想觀察 SRTF 優勢，就安排陸續進來的短工作
- 若想觀察 Priority 飢餓，就安排持續較高優先權的工作

---

## 9. Benchmark 結果分析

以下數據來自 `results/benchmark.csv`：

| Algorithm | AWT | ATT | ART |
|---|---:|---:|---:|
| FCFS | 21.8333 | 26.5833 | 21.8333 |
| SJF_NonPreemptive | 14.5833 | 19.3333 | 14.5833 |
| SRTF_Preemptive | 13.3333 | 18.0833 | 11.4167 |
| Priority_NonPreemptive | 18.3333 | 23.0833 | 18.3333 |
| RoundRobin_Q1 | 26.8333 | 31.5833 | 4.0833 |
| RoundRobin_Q2 | 27.3333 | 32.0833 | 8.2500 |
| RoundRobin_Q4 | 25.3333 | 30.0833 | 15.0833 |
| RoundRobin_Q8 | 22.5000 | 27.2500 | 21.5833 |

### 9.1 這份 benchmark 告訴我們什麼

#### SRTF 在 AWT 與 ATT 最好

這符合排程理論。  
SRTF 持續優先處理剩餘時間短的工作，因此在這組 workload 中，平均等待與平均週轉都最低。

#### RR Q=1 的 ART 最好

這也很合理。  
因為每個 process 幾乎都能很快先輪到一次，所以首次回應延遲最低。

#### RR 的 Q 變大後，行為接近 FCFS

從 `Q=1 -> Q=8` 可以看到：

- ART 逐漸變差
- AWT/ATT 往 FCFS 靠近

這不是巧合，而是 RR 的本質。

### 9.2 一個很值得教學強調的觀察

在本專案目前的 simulator 中，RR 並沒有因為「實際計算的 context switch 開銷」而變差，因為程式沒有把這個成本顯式加進去。  
即使如此，Q 很小時 AWT/ATT 仍不漂亮，原因是工作被切得更碎，很多長工作會被反覆推遲完成時間。

這是「排程次序效應」本身造成的，不需要真的把 kernel context switch cost 加進模型，也能觀察到一部分現象。

---

## 10. 時間複雜度與工程取捨

### 10.1 FCFS

- 主要成本：排序
- 複雜度：約 `O(n log n)`

### 10.2 SJF / Priority

- 每次選下一個工作時都線性掃描
- 若有 `n` 個 process，整體約可視為 `O(n^2)`

### 10.3 SRTF

- 每個 time unit 都掃描所有 process
- 若總模擬時間是 `T`，複雜度約 `O(T * n)`

### 10.4 Round Robin

- 每次取出 process 後，還會掃描是否有新 arrival
- 在簡化分析下，也可視為 `O(T * n)` 等級

### 10.5 為什麼仍然是合理設計

這份專案不是在追求極大規模工作負載效率，而是在追求：

- 演算法可讀性
- Trace 可觀察性
- 教學可驗證性

若改用 heap、balanced tree、事件佇列，效能可以更好，但報告、教學、debug 成本也會上升。

因此這是一個典型的 teaching-oriented implementation（教學導向實作）。

---

## 11. VM 建置與自動化技術深入探討

## 11.1 `01_setup_env.sh` 的技術重點

這支腳本不是單純下載映像檔而已，它實際做了幾件有工程意義的事情：

1. 檢查依賴工具是否存在
2. 必要時透過 `apt-get` 安裝工具
3. 下載 Ubuntu cloud image
4. 轉換與擴充 qcow2 磁碟
5. 在 host 端先編譯 `scheduler.c`
6. 將 binary、source、workload 做 base64 編碼
7. 生成 cloud-init `user-data` 與 `meta-data`
8. 用 `cloud-localds` 產生 seed ISO

### 為什麼先在 host 端編譯一次

這是一個很實際的品質保證做法。

如果 `scheduler.c` 本身就有語法錯誤，那 VM 就算啟動成功也沒有意義。  
先在 host 端 `gcc -O2 -Wall -Wextra -std=c11` 編譯，可提早發現問題。

### 為什麼把 binary 直接塞進 cloud-init

本專案不是在 VM 裡再重新編譯，而是把 host 已驗證可執行的 binary 與 source 一起注入 VM。  
這有幾個優點：

- 首次開機後可立即執行
- 不依賴 VM 內部安裝 build toolchain
- source 與 binary 同時保留，便於對照與教學

## 11.2 `02_start_vm.sh` 的技術重點

這支腳本會根據 `/dev/kvm` 是否可讀寫，自動決定：

- `kvm + q35 + host CPU`
- 或 `tcg + pc + qemu64 CPU`

這代表腳本有考慮「硬體加速可用」與「純軟體模擬 fallback」兩條路徑。

這種設計很實用，因為不是每個環境都有 KVM。

### 為什麼要等 SSH 與 `.setup_done`

VM 開機不等於 scheduler 已準備好。

腳本先等：

- SSH 可連線

再等：

- `/home/scheduler/.setup_done` 存在且 `scheduler` binary 可執行

這代表它區分了：

- 作業系統已開機
- 應用程式已佈署完成

這是很重要的自動化流程觀念。

## 11.3 `03_demo.sh` 與 `04_benchmark.sh`

這兩支腳本的價值在於把「執行模擬器」升級成「有固定流程的實驗」。

`03_demo.sh` 偏展示：

- 列出 demo workload
- 逐一執行演算法
- 用 `tee` 同時顯示並寫入 `results/demo_output.txt`

`04_benchmark.sh` 偏量化：

- 執行所有演算法
- 擷取結構化指標
- 產出 `benchmark.csv`
- 產出文字化彙總報告

這兩者分工清楚，設計合理。

---

## 12. 關鍵名詞中英文對照與深入說明

| 中文 | English | 深入說明 |
|---|---|---|
| 行程 | Process | 本專案用一筆 `Process` 結構描述一個可被排程的工作單位 |
| 排程器 | Scheduler | 決定下一個誰拿 CPU 的邏輯 |
| 排程演算法 | Scheduling Algorithm | 如 FCFS、SJF、SRTF、RR |
| 到達時間 | Arrival Time | process 進入系統、開始具備排程資格的時間 |
| CPU 執行時間 | CPU Burst Time | process 需要的 CPU 計算量，在本專案中用整數時間單位表示 |
| 剩餘時間 | Remaining Time | 可搶先演算法判斷是否換人的核心依據 |
| 優先權 | Priority | 本專案中數字越小優先級越高 |
| 可搶先 | Preemptive | 執行中的 process 可能被中途打斷 |
| 不可搶先 | Non-preemptive | 一旦開始執行就跑到完成或量子結束 |
| 等待時間 | Waiting Time | process 在 ready queue 中等待 CPU 的總時間 |
| 週轉時間 | Turnaround Time | 從 arrival 到 completion 的完整時間 |
| 回應時間 | Response Time | 從 arrival 到首次拿到 CPU 的時間 |
| 護送效應 | Convoy Effect | 長工作卡住短工作，常見於 FCFS |
| 飢餓 | Starvation | 某些工作因選擇規則長期拿不到 CPU |
| 時間片 / 時間量子 | Time Quantum | RR 每次允許執行的最長時間 |
| 就緒佇列 | Ready Queue | 等待被選上 CPU 的 process 集合 |
| 甘特圖 | Gantt Chart | 本專案用來展示排程時間線的文字圖 |
| 執行軌跡 | Execution Trace | process 在不同時間區段的執行情況紀錄 |
| 結構化輸出 | Structured Output | 給腳本穩定解析的固定格式結果，例如 `BENCHMARK` 行 |
| 虛擬機器 | Virtual Machine | 本專案用 QEMU 建立隔離環境 |
| 雲端初始化 | Cloud-init | Ubuntu cloud image 首次開機自動設定機制 |

---

## 13. 具體例子：為什麼 SRTF 的 Trace 最值得看

這裡直接以 demo workload 做教學：

```text
P1  arrival=0 burst=8
P2  arrival=1 burst=4
P3  arrival=2 burst=9
P4  arrival=3 burst=5
P5  arrival=4 burst=2
P6  arrival=5 burst=1
```

### 時間推進解析

#### `t=0`

只有 `P1` 存在，所以 `P1` 執行。  
Trace 增加：

```text
(P1,0,1)
```

#### `t=1`

`P2` 到達。  
此時：

- `P1` remaining = 7
- `P2` remaining = 4

所以 SRTF 改選 `P2`。  
這就是 preemption。

Trace 變成：

```text
(P1,0,1), (P2,1,2)
```

#### `t=2,3,4`

雖然 `P3`、`P4`、`P5` 先後到達，但 `P2` remaining 仍最小，因此繼續執行。  
`gantt_push()` 會把 `(P2,1,2)` 合併成 `(P2,1,5)`。

#### `t=5`

`P2` 完成，`P6` 到達，且 `P6` burst=1 最短。  
所以執行 `P6`。

#### 後續

再依序選 `P5`、`P4`、`P1`、`P3`。

### 最終 Gantt Chart

```text
| P1 | P2 | P6 | P5 | P4 | P1 | P3 |
0    1    5    6    8    13   20   29
```

### 這個例子教會了什麼

1. SRTF 並不是只看誰先來。
2. SRTF 真正比較的是 remaining time，不是原始 burst。
3. Trace 若沒有逐時間單位紀錄，就很難驗證 preemption 是否正確。
4. `gantt_push()` 的合併機制，讓底層逐 tick 模擬可以被人類讀懂。

---

## 14. 具體例子：Round Robin 的 Q 值如何改變 Trace

同樣看 demo workload。

### Q = 1

每次只跑 1 單位，因此 Gantt Chart 會非常碎：

```text
| P1 | P2 | P1 | P3 | P2 | P4 | P1 | P5 | ... |
```

這代表：

- 幾乎所有 process 都很快有第一次回應
- 但完成時間可能被拖得較長

對應結果：

- `ART = 1.6667`
- 明顯優於 FCFS 的 `ART = 13.3333`

### Q = 8

Q 已經大到接近長工作可以一輪跑完，因此行為更接近 FCFS：

```text
| P1 | P2 | P3 | P4 | P5 | P6 | P3 |
```

此時：

- response time 明顯變差
- 排程切換次數變少
- 行為更加接近 arrival 順序 + 長片段執行

### 教學結論

RR 的 Q 不只是參數，它是「互動性與整體完成效率之間的調節旋鈕（control knob）」。

---

## 15. 本專案的限制與可延伸方向

### 15.1 現有限制

1. 單核心模型
   本專案沒有模擬多核心 CPU，因此不存在真正的平行執行。

2. 整數時間模型
   所有時間都以整數 tick 處理，沒有更細時間精度。

3. 無 I/O blocking
   process 只有 arrival 與 CPU burst，沒有阻塞、喚醒、I/O wait。

4. 無 context switch cost
   切換本身沒有顯式時間成本。

5. 無 aging、無動態 priority 調整
   因此 Priority Scheduling 可能有飢餓風險。

6. Trace 偏結果導向
   現在看得到「誰在什麼時候跑」，但還看不到「為什麼這一刻做這個決策」。

### 15.2 可延伸方向

若要把這個專案升級成更進階版本，可以考慮：

1. 記錄每次 preemption reason
2. 額外輸出 ready queue snapshot
3. 計算 context switch count
4. 模擬 context switch overhead
5. 加入 aging 機制
6. 加入 multi-core scheduling
7. 將 Trace 輸出成 JSON 或 CSV event log
8. 若要真正接近 Linux kernel，可再引入 `ftrace`、`perf`、`eBPF`

---

## 16. 結論重點

1. 這個專案雖然主題是 CPU Scheduling，但核心能力其實很接近 firmware engineering：狀態管理、時間推進、決策可驗證、Trace 設計、自動化驗證。

2. `scheduler.c` 的價值不只在於算出 AWT、ATT、ART，而在於它把排程決策過程具體保留下來，讓使用者可以檢查「為什麼得到這個結果」。

3. `GanttSlot`、`gantt_push()`、`print_gantt()`、`BENCHMARK` 行共同構成了本專案最重要的 observability 機制。若從 firmware 技能來看，這部分比單純演算法背誦更有工程價值。

4. SRTF 與 RR 最能展示 Trace 的必要性。前者呈現 preemption，後者呈現 quantum 對互動性與完成時間的拉扯。沒有 Trace，就很難做精確驗證。

5. QEMU、cloud-init、SSH、自動化腳本的組合，讓這個專案具備可重現測試環境。這種能力與 firmware 團隊常見的 virtual platform testing、hardware bring-up automation、CI regression testing 非常接近。

6. 本專案目前不是 kernel-level tracing，也沒有模擬真實 context switch overhead，但這不影響它作為教學平台的價值。相反地，因為模型簡潔，才更容易看清楚 scheduling policy 本身如何影響時間行為。

7. 如果把這份專案延伸到更進階版本，最有價值的方向不是只多加幾個演算法，而是把 Trace 提升為更完整的 event log、queue snapshot、preemption reason、context-switch accounting。這會讓它更接近真實 firmware 與 embedded observability workflow。

## 17. 總結

這個專案雖然規模不大，但技術結構其實很完整。它不是只把幾個演算法塞進一支 C 程式，而是把：

- 演算法實作
- 可重現虛擬環境
- 自動化執行
- Trace 觀測
- Benchmark 數據輸出

整合成一個教學型實驗平台。

若只看排程理論，學生容易停留在公式。  
若只看最終平均值，學生又會忽略排程過程。  
本專案真正有價值的地方，在於它把「排程過程」透過 `GanttSlot`、`gantt_push()`、`print_gantt()`、`BENCHMARK` 行具體化，讓 Trace 不再只是抽象概念，而是可以被看見、被驗證、被比較的實際資料。

若要用一句話總結本專案：

> 這是一個以 C 為核心、以 QEMU 為實驗場、以 Trace 與 Benchmark 為觀測手段的 CPU Scheduling 教學型模擬平台。

若改用 firmware 的語言來描述，則可以更精確地說：

> 這是一個用 scheduler 模型來訓練 firmware engineer 核心能力的實驗平台，重點不只是演算法，而是狀態、時間、Trace、可驗證性與自動化。
