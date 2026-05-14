# CPU Scheduling Simulator: QEMU-Based Reproducible Environment

[![Kernel Version](https://img.shields.io/badge/Reproduction-Ubuntu%2024.04-orange.svg)](https://ubuntu.com)
[![QEMU](https://img.shields.io/badge/Platform-QEMU%20x86__64-blue.svg)](https://www.qemu.org/)
[![License](https://img.shields.io/badge/License-GPL--2.0-green.svg)](LICENSE)

本專案提供了一個高度可重現的 CPU 排程演算法實驗平台。透過 C 語言實作核心排程邏輯，並利用 **QEMU 虛擬化技術** 與 **Cloud-init 自動化佈署**，讓開發者能在一個完全隔離且乾淨的環境中，精確比較不同排程演算法（如 FCFS, SJF, SRTF, RR 等）的效能表現。

這不僅是一個演算法實作，更是一套完整的系統驗證流水線，從環境建置、自動化測試到數據視覺化彙整，模擬了真實系統開發中的品質驗證流程。

---

## 🎯 專案核心目標

1.  **純 C 實作排程器**：包含 FCFS、SJF、SRTF、Priority 與不同 Time Quantum 的 Round Robin。
2.  **隔離實驗環境**：使用 QEMU + Ubuntu 24.04 Cloud Image，確保所有主機執行結果一致。
3.  **自動化測試管線**：一鍵完成 VM 啟動、Workload 載入、效能數據（AWT, ATT, ART）擷取。
4.  **視覺化軌跡分析**：自動生成純文字 Gantt Chart，直觀觀察搶先（Preemption）與時間片切換。

---

## 📂 專案架構

```text
cpu-scheduling-qemu/
├── src/                # 排程模擬器原始碼與測試負載
│   ├── scheduler.c     # 核心邏輯 (FCFS, SJF, SRTF, Priority, RR)
│   └── workload_*.txt  # 預定義的測試案例 (Demo/Benchmark)
├── scripts/            # 自動化管理腳本 (01-05 依序執行)
├── results/            # 實驗結果匯出 (CSV, Report, Logs)
├── vm/                 # QEMU 虛擬機器檔案 (由腳本動態生成)
└── Makefile            # 流程調度定義
```

---

## 🛠️ 開發環境準備

本專案建議在 **Ubuntu 24.04 (x86_64)** Host 環境下執行。腳本會自動檢查並補足缺失工具，但手動預裝以下套件可確保流程更順暢：

```bash
sudo apt update
sudo apt install -y qemu-system-x86 qemu-utils cloud-image-utils \
                    libguestfs-tools gcc wget sshpass bc
```

*註：若您的環境支援 KVM 加速（`/dev/kvm` 可讀寫），VM 執行速度將提升 10 倍以上。*

---

## 🚀 逐步執行教學

### 第一步：建置實驗環境
此步驟會下載 Ubuntu Cloud Image 並透過 Cloud-init 注入排程器二進位檔。

```bash
# 賦予腳本執行權限
chmod +x scripts/*.sh

# 執行環境建置 (約需 2-5 分鐘，視網路速度而定)
bash scripts/01_setup_env.sh
```

### 第二步：啟動虛擬機器
VM 將以 **Daemon 模式** 在背景啟動。

```bash
bash scripts/02_start_vm.sh
```
*腳本會持續偵測 SSH 狀態，直到 VM 內部初始化完成。啟動後您可以透過 `ssh -p 2222 scheduler@localhost` 手動登入觀察（密碼：`scheduler123`）。*

### 第三步：執行演算法展示 (Demo)
此步驟會將 Demo Workload 餵給模擬器，並輸出直觀的甘特圖。

```bash
bash scripts/03_demo.sh
```
*結果將儲存於 `results/demo_output.txt`。建議開啟另一個終端機執行 `tail -f results/demo_output.txt` 即時觀察。*

### 第四步：效能基準測試 (Benchmark)
針對 12 個 Processes 的複雜場景，自動計算各演算法的平均等待時間與週轉時間。

```bash
bash scripts/04_benchmark.sh
```
*完成後請查看 `results/benchmark_report.txt` 取得詳細的對比分析表。*

### 第五步：環境清理
結束實驗後，釋放磁碟空間與背景行程。

```bash
bash scripts/05_cleanup.sh
```

---

## 📊 關鍵指標定義

-   **AWT (Average Waiting Time)**：行程在 Ready Queue 中等待取得 CPU 的平均時間。
-   **ATT (Average Turnaround Time)**：從行程到達至完全結束的平均生命週期。
-   **ART (Average Response Time)**：從行程到達至**第一次**取得 CPU 的平均延遲。

---

## 🔍 未來擴充方向

-   **多核心排程模擬 (Multi-core Scheduling)**：擴充資料結構以支援多個處理單元的任務分配（Load Balancing）。
-   **加入 Context Switch 開銷**：在模擬模型中計入任務切換造成的 Tick 損耗，使結果更貼近真實 OS。
-   **老化機制 (Aging)**：針對 Priority Scheduling 實作動態權重調整，解決飢餓（Starvation）問題。
-   **視覺化 Web Dashboard**：將 CSV 數據對接 Grafana 或 Python 圖表，自動產出效能對比圖形。
