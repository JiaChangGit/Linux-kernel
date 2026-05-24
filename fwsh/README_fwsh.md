# fwsh (Firmware Mini Shell)

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux-orange.svg)](https://www.linux.org/)
[![Language](https://img.shields.io/badge/Language-C11-green.svg)](https://en.cppreference.com/w/c/11)

`fwsh` 是一款專為韌體工程師（Firmware Engineer）設計的輕量級 Linux Shell。它不僅具備標準 Shell 的核心功能（Pipe, Redirection, Background Execution），更整合了多項底層開發與除錯常用的工具，如 **Hexdump**、**CRC-32 計算** 與 **實體記憶體佈局分析 (Memmap)**。

透過 `fwsh`，開發者可以在不依賴外部龐大工具套件的情況下，直接在 Shell 環境中對韌體映像檔進行分析與驗證，是學習作業系統原理與韌體開發的最佳實戰案例。

---

## 🚀 核心特色

-   **標準 Shell 功能**：支援多段管線 (`|`)、輸入輸出重導向 (`<`, `>`, `>>`) 以及背景執行 (`&`)。
-   **現代化互動介面**：整合 GNU Readline，支援 Tab 自動補全、上下鍵歷史記錄搜尋 (Ctrl+R) 與行編輯。
-   **韌體開發專屬工具**：
    -   `hexdump`: 支援十六進位與 ASCII 雙欄對照，除錯 Binary 映像檔必備。
    -   `crc32`: 實作 IEEE 802.3 CRC-32 演算法，快速驗證韌體完整性。
    -   `memmap`: 解析 `/proc/iomem`，視覺化顯示目標板的實體記憶體配置。
-   **健全的訊號處理**：完善處理 `Ctrl+C` (SIGINT) 與非同步子行程回收 (SIGCHLD)，防止殭屍行程 (Zombie Process) 產生。

---

## 📂 專案架構

```text
fwsh/
├── include/           # 標頭檔 (API 定義與資料結構)
│   ├── builtin.h      # 內建指令模組
│   ├── executor.h     # 執行引擎模組
│   ├── parser.h       # 詞法分析與語法解析
│   └── shell.h        # 全域狀態與常數定義
├── src/               # 原始碼
│   ├── builtin.c      # 韌體工具與 Shell 指令實作
│   ├── executor.c     # fork/exec/pipe 核心實作
│   ├── parser.c       # 支援引號處理的 Lexer
│   ├── shell.c        # REPL 主迴圈與訊號處理
│   └── main.c         # 程式入口點
└── docs/              # 執行截圖與展示文件
```

---

## 🛠️ 建置與安裝

### 前置需求
`fwsh` 依賴 `libreadline` 庫來提供互動編輯功能。在 Ubuntu/Debian 系統上請先安裝開發套件：

```bash
sudo apt update
sudo apt install -y libreadline-dev
```

### 編譯
進入 `fwsh` 子目錄並執行以下編譯指令：

```bash
# 確保位於 fwsh 目錄下
gcc -Wall -Iinclude src/*.c -o fwsh -lreadline
```

---

## 🎬 實機操作演示 (DEMO)

### 1. 啟動 Shell
```bash
./fwsh
```
啟動後會看到專屬的 Banner 與彩色提示字元 `[fwsh user@hostname ~/path]$`。

### 2. 測試管線與重導向
您可以嘗試組合標準 Linux 指令與 `fwsh` 的內建工具：
```bash
# 列出檔案並透過管線傳給 grep，最後儲存到檔案
ls -la | grep "fwsh" > output.txt

# 將剛剛產生的檔案作為輸入
cat < output.txt
```

### 3. 韌體開發工具展示
這是 `fwsh` 最具特色的部分：

- **Hexdump 檔案內容**：
  ```bash
  # 檢視當前二進位檔的前 0x80 bytes
  hexdump ./fwsh 0x80
  ```
- **計算 CRC-32**：
  ```bash
  # 驗證檔案完整性
  crc32 ./fwsh
  ```
- **查看系統記憶體佈局**：
  ```bash
  # 視覺化 /proc/iomem
  memmap
  ```

### 4. 背景執行與訊號測試
- 按下 `Ctrl+C`：`fwsh` 會清除當前輸入行並顯示新提示字元，而不會崩潰退出。
- 執行背景指令：`sleep 10 &`。


### 5. Leave
- 按下 `Ctrl+D` 
- `make clean` 

---

## 📌 未來擴充方向

- **環境變數展開**：支援 `$PATH` 與 `$HOME` 等變數在命令列中的自動替換。
- **內建 Debugger 介面**：整合 GDB stub 或透過 `/dev/mem` 實作簡易的暫存器讀寫功能。
- **腳本支援**：支援執行 `.sh` 或專屬的 `.fws` 腳本檔案。
- **Job Control**：實作 `jobs`、`fg`、`bg` 指令來精確管理背景工作。
