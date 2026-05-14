# Linux Kernel & Firmware Engineering Portfolio

[![Kernel Version](https://img.shields.io/badge/Kernel-5.15%2F6.8-blue.svg)](https://kernel.org)
[![Platform](https://img.shields.io/badge/Platform-Ubuntu%2022.04%2F24.04-orange.svg)](https://ubuntu.com)
[![License](https://img.shields.io/badge/License-GPL--2.0-green.svg)](LICENSE)

本儲存庫整合了多個深入 Linux 核心開發、韌體工程與系統編程的實作專案。涵蓋範圍從基礎的字元驅動程式、作業系統排程演算法模擬，到高效能的行程間通訊 (IPC) 分析以及 ARM64 平台硬體點亮 (Bring-up) 流程。

這不只是一個程式碼集合，而是一套完整的系統工程實踐紀錄，旨在展示如何解決真實世界中的核心層級挑戰。

---

## 📂 子專案概覽

| 專案名稱 | 技術重點 | 核心價值 |
| :--- | :--- | :--- |
| [**chardev-driver**](./chardev-driver/) | VFS, ioctl, procfs, sysfs, Mutex | 實作具備多重管理介面的工業級字元驅動。 |
| [**cpu-scheduling-qemu**](./cpu-scheduling-qemu/) | C11, QEMU, Cloud-init, Discrete Sim | 在隔離環境中量化分析 CPU 排程演算法效能。 |
| [**fwsh (Firmware Shell)**](./fwsh/) | fork/exec/pipe, Lexer, CRC-32, MMIO | 建構整合韌體除錯工具的微型互動式 Shell。 |
| [**linux-ipc-benchmark**](./linux-ipc-benchmark/) | kfifo, mmap, Zero-copy, Memory Barrier | 深度對照 Message Queue 與共享記憶體的效能差異。 |
| [**qemu-platform-demo**](./qemu-platform-demo/) | Platform Bus, Device Tree, devm, ARM64 | 模擬完整硬體點亮流程與裝置樹動態注入技術。 |

---

## 🛠️ 全域環境準備

建議使用 **Ubuntu 22.04/24.04 LTS (x86_64)**。請先安裝基礎開發工具組：

```bash
sudo apt update
sudo apt install -y build-essential gcc-aarch64-linux-gnu \
                    qemu-system-x86 qemu-system-arm \
                    device-tree-compiler kmod bc bison flex \
                    libssl-dev libelf-dev wget libreadline-dev
```

---

## 🚀 快速上手與導覽

本儲存庫各子專案皆為獨立模組。您可以依據興趣選擇特定專案進行實驗。

### 1. 核心驅動實戰 (chardev-driver)
學習如何安全地處理核心/使用者空間資料交換，並利用 `/proc` 與 `/sys` 監控驅動狀態。
- **DEMO**：進入目錄執行 `sudo bash scripts/load.sh`，隨後於另一視窗執行 `sudo dmesg -w` 觀察。

### 2. 系統排程實驗室 (cpu-scheduling-qemu)
透過自動化腳本建立 QEMU 虛擬環境，一鍵跑完 SRTF、RR 等演算法的基準測試 (Benchmark)。
- **DEMO**：進入目錄執行 `bash scripts/01_setup_env.sh`，隨後啟動 VM 進行測試。

### 3. 韌體除錯工具 (fwsh)
體驗專為韌體工程師設計的 Shell，內建 CRC-32 計算與 Hexdump 功能。
- **DEMO**：進入目錄編譯後執行 `./fwsh`。

### 4. IPC 效能極限測試 (linux-ipc-benchmark)
實測「零拷貝 (Zero-copy)」技術如何將通訊效率提升 5 倍以上。
- **DEMO**：進入目錄執行 `sudo bash scripts/01_setup.sh`，隨後執行 `./user/benchmark`。

### 5. ARM64 平台開發 (qemu-platform-demo)
模擬真實的嵌入式開發，從編譯 ARM64 核心到注入裝置樹片段 (DTS Fragment)。
- **DEMO**：進入目錄依序執行 `01` 至 `05` 號腳本，進入 QEMU 虛擬系統。

---

## 📜 綜合技術報告

如果您想從宏觀角度了解這些專案背後的系統設計思維（如同步機制選型、記憶體管理策略與 VFS 架構），請參閱：
👉 [**全專案綜合技術報告 (report.md)**](report.md)

---

## 📌 未來擴充方向

1.  **支援 eBPF 遙測**：導入 `bcc` 或 `bpftrace` 來分析 IPC 與驅動的微觀效能。
2.  **多核心擴展**：將排程器模擬器擴展至多核心負載平衡 (Load Balancing) 場景。
3.  **異質通訊**：在 `qemu-platform-demo` 中加入虛擬的 Mailbox 機制，模擬多處理器間通訊。

---

## ⚖️ 授權條款
本儲存庫所有核心程式碼皆採用 **GPL-2.0** 授權，使用者空間工具則遵循 **MIT** 授權。
