# CPU 排程模擬系統：技術架構與實作深度解析報告

本報告針對 `cpu-scheduling-qemu` 專案進行全方位的技術分析。本專案不單只是演算法的練習，它展現了如何透過「虛擬化環境 (Virtualization)、自動化部署 (Automated Provisioning)、結構化追蹤 (Structured Tracing)」三者結合，建立一個標準的核心邏輯開發與驗證平台。

---

## 1. 系統架構與技術組成

本專案採用的「主機-訪客 (Host-Guest) 隔離架構」是其核心特色，主要由以下四層組成：

-   **演算法層 (Core Logic Layer)**：使用 C 語言 (C11) 實作的 `scheduler.c`。這是一個離散時間模擬器 (Discrete Event Simulator)，負責處理排程狀態轉移。
-   **虛擬化層 (Virtualization Layer)**：使用 QEMU (x86_64) 提供一個乾淨、不可變 (Immutable) 的實驗環境，確保環境相依性 (Dependency) 被完全隔離。
-   **部署層 (Provisioning Layer)**：利用 Cloud-init (Seed ISO) 將主機編譯好的二進位檔 (Binary) 注入虛擬機器 (VM)。這模擬了韌體 (Firmware) 開發中「Host 端編譯、Target 端執行」的常見模式。
-   **驗證層 (Benchmarking Layer)**：透過 Bash 腳本將多個實驗組（不同演算法）串聯，並利用 `sshpass` 與 `bc` 進行數據擷取與浮點數分析。

---

## 2. 核心資料結構與狀態模型

在 `scheduler.c` 中，系統狀態由兩個關鍵結構體維護：

### 2.1 `Process` 結構：行程狀態機 (Process State Machine)
這是模擬器的靈魂，記錄了行程生命週期中的所有時間節點：
-   `burst` vs `remaining`：區分原始需求與目前剩餘執行時間（SRTF/RR 的判斷關鍵）。
-   `start` & `responded`：用於精確記錄首次取得處理器 (CPU) 的時間點，計算回應時間 (Response Time, ART)。
-   `finish`：標記行程完成點，結合到達時間 (Arrival Time) 導出週轉時間 (Turnaround Time, ATT)。

### 2.2 `GanttSlot` 結構：執行軌跡追蹤 (Execution Tracing)
為了視覺化排程過程，系統實作了輕量級的軌跡紀錄功能。
-   **合併機制 (Trace Consolidation)**：若前後兩個時間片段的行程識別碼 (PID) 相同，系統會自動合併時間區間，而非新增節點。這使得 SRTF 這類以時鐘滴答 (Tick) 為單位的模擬器，也能產出易讀的甘特圖 (Gantt Chart)。

---

## 3. 排程演算法之實作深度分析

### 3.1 先來先服務 (First-Come First-Served, FCFS)
-   **原理**：非搶先式 (Non-preemptive)。單純依 `arrival` 時間排序。
-   **觀察**：最簡單但也最容易發生「護送效應 (Convoy Effect)」，長行程會嚴重阻礙後方短行程的進入，導致平均等待時間 (AWT) 飆升。

### 3.2 最短工作優先 (Shortest Job First, SJF)
-   **原理**：非搶先式。在 CPU 空閒時，從所有已就緒 (Ready) 的行程中選取執行時間最短者。
-   **核心邏輯**：使用 `done[]` 標記位。若目前無行程就緒，則時鐘 (`clock`) 直接跳轉至下一個行程的到達時刻，避免無意義的循環。

### 3.3 最短剩餘時間優先 (Shortest Remaining Time First, SRTF)
-   **技術重點**：**搶先式 (Preemptive) 實作**。
-   **執行流**：採逐滴答 (Tick-by-Tick) 模擬。每個時間單位都重新掃描所有剩餘時間大於零的行程，並選取最小者執行。這會觸發 `gantt_push()` 的合併邏輯，紀錄下每一次搶先發生的時刻。

### 3.4 輪轉排程 (Round Robin, RR)
-   **原理**：時間片 (Time Quantum) 輪轉。使用 `queue[]` 實作先進先出 (FIFO) 的就緒佇列。
-   **實作挑戰**：
    -   **入隊優先權**：當行程用完時間片但尚未結束時，必須先處理「同時到達」的新行程入隊，再將舊行程放回隊尾。這對於系統公平性 (Fairness) 的模擬至關重要。

---

## 4. 關鍵指標計算公式與意義

-   **週轉時間 (Turnaround Time, TAT)** = `Finish - Arrival` (從進入系統到完全結束的時間)。
-   **等待時間 (Waiting Time, WT)** = `Turnaround - Burst` (在就緒佇列中虛度光陰的時間)。
-   **回應時間 (Response Time, RT)** = `Start - Arrival` (從進入系統到第一次被服務的時間)。

在 `scripts/04_benchmark.sh` 的對比中，**SRTF** 通常能獲得理論最優的 AWT，而 **RR (小時間片)** 則在互動性 (ART) 上表現最佳。

---

## 5. 開發難點與除錯挑戰 (Troubleshooting)

1.  **Cloud-init 同步競態 (Race Condition)**：
    早期版本常發生 SSH 已可連線但內部腳本尚未部署完畢。透過引入 `.setup_done` 標記檔案檢測，解決了自動化測試的同步問題。
2.  **浮點數運算開銷**：
    Bash 不支援浮點數，因此所有 AWT/ATT 的對比與邏輯判斷皆透由 `bc -l` (Arbitrary Precision Calculator) 處理，確保基準測試數據的精確度。

---

## 6. 橫向技術對比：模擬器 vs 核心追蹤

本專案與真正的 Linux 核心排程追蹤有本質不同：
-   **本專案**：屬於 **離散事件模擬 (Discrete Event Simulation)**。在受控模型下觀察演算法決策。
-   **核心追蹤 (ftrace/eBPF)**：觀測的是 **實體系統行為**，會受到中斷 (Interrupt)、快取缺失 (Cache Miss) 等非預期干擾。

---

## 7. 總結與技術延伸

`cpu-scheduling-qemu` 展現了一個工業級驗證框架的雛形。未來可朝向 **多核心負載平衡 (Load Balancing)**、**老化機制 (Aging)** 以解決飢餓問題，或是在模擬模型中計入 **行程切換成本 (Context Switch Overhead)** 來提升真實感。
