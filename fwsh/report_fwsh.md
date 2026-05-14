# fwsh (Firmware Mini Shell) 技術實作報告

## 1. 專案概述與核心技術

`fwsh` 是一個遵循 POSIX 標準實作的微型 Shell，開發重點在於理解作業系統的核心機制：行程管理 (Process Management)、行程間通訊 (IPC) 與訊號處理 (Signal Handling)。除了傳統 Shell 的功能，本專案特別針對韌體工程師的需求，實作了直接存取系統層級資訊與二進位 (Binary) 分析的工具。

### 核心技術點：
- **行程模型 (Process Model)**：利用 `fork()` 建立子行程，配合 `execvp()` 載入外部程式。
- **管線機制 (Pipelining)**：實作 `pipe()` 與 `dup2()` 達成多行程間的匿名管線通訊。
- **訊號處理 (Signal Handling)**：非同步回收殭屍行程 (Zombie Process)，並保護 Shell 不受 `Ctrl+C` 直接終止。
- **詞法分析 (Parsing)**：實作支援「引號狀態」的詞法分析器 (Lexer)，正確解析包含空白的字串引數。
- **內建工具 (Built-in Tools)**：使用「查表法 (Table-driven)」優化 CRC-32 計算。

---

## 2. 軟體架構與模組設計

本專案採用高度模組化設計，主要分為四大組件：

1.  **Shell 核心 (`shell.c`)**：負責 REPL (Read-Eval-Print Loop) 主迴圈、提示字元生成與訊號初始化。
2.  **解析器 (`parser.c`)**：將原始字串轉換為 `Pipeline` 結構。
3.  **執行引擎 (`executor.c`)**：處理 `fork`、`pipe` 串接與 I/O 重導向 (Redirection)。
4.  **內建指令庫 (`builtin.c`)**：包含 Shell 邏輯指令與韌體工程師專屬工具。

---

## 3. 核心功能實作原理分析

### 3.1 詞法分析與語法解析 (Parsing)
`parse_line()` 函式內部使用 `Lexer` 結構維護解析狀態。
-   **引號處理**：當 Lexer 偵測到 `'` 或 `"` 時，會進入字串模式。在單引號內，所有字元（包含空白）皆視為一般文字；雙引號則支援 `\"` 轉義 (Escape)。
-   **符號識別**：當偵測到 `|`、`<`、`>`、`&` 等特殊符號時，Lexer 會立即終止當前詞 (Word) 的讀取，並根據符號更新 `Cmd` 或 `Pipeline` 結構。

### 3.2 管線與執行引擎 (Execution)
`execute_pipeline()` 是最複雜的部分，處理多段指令的鏈接。
-   **管線建立**：根據 `ncmds` 預先建立 `n-1` 個 `pipe` 檔案描述符 (File Descriptor) 陣列。
-   **接線邏輯**：
    -   第 $i$ 個子行程會將 `pipes[i-1][0]` (讀端) `dup2` 到 `stdin`。
    -   將 `pipes[i][1]` (寫端) `dup2` 到 `stdout`。
-   **死結防範**：父行程與子行程都必須在 `dup2` 完成後關閉所有不屬於自己的管線 fd。**若父行程未關閉管線寫端，則管線末端的子行程將永遠收不到 EOF，導致 Shell 永久阻塞。**

### 3.3 訊號處理 (Signals)
-   **SIGCHLD**：背景執行時，Shell 不會調用 `waitpid()` 阻塞等待。我們實作了 `sigchld_handler`，利用 `while(waitpid(-1, NULL, WNOHANG) > 0)` 在訊號觸發時回收所有已結束的子行程，避免行程識別碼 (PID) 資源耗盡。
-   **SIGINT**：利用 Readline 的 `rl_on_new_line()` 與 `rl_redisplay()` 讓 Shell 在收到 `Ctrl+C` 時僅清空輸入行，維持良好的互動體驗。

---

## 4. 關鍵函式與實作對比

### 4.1 內建指令 vs 外部指令
| 特性 | 內建指令 (Built-in) | 外部指令 (External) |
| :--- | :--- | :--- |
| **執行行程** | Shell 行程本身 (Current Process) | fork 出的子行程 (Child Process) |
| **環境影響** | 可修改 Shell 狀態（如 `cd` 改變工作目錄） | 狀態修改僅限於子行程，結束後消失 |
| **分派方式** | 透過 `BuiltinEntry` 分派表匹配函式指標 | 透過 `execvp` 搜尋 `$PATH` 執行 |

### 4.2 韌體工具實作細節
-   **`hexdump`**：採用 16-byte 為單位的迴圈。核心技巧在於判斷 `isprint()` 以正確顯示 ASCII 預覽欄位，並支援 `0x` 前置的十六進位長度參數解析。
-   **`crc32`**：實作了 IEEE 802.3 查表法。
    ```c
    // 核心更新邏輯
    crc = crc32_table[(crc ^ chunk[i]) & 0xFF] ^ (crc >> 8);
    ```
    相對於傳統逐位元運算，查表法將效率提升了 8 倍。

---

## 5. 開發挑戰與除錯紀錄

### 5.1 殭屍行程問題
在開發初期，執行 `&` 背景指令後，子行程結束會殘留在系統中。
-   **解決方案**：引入 `SA_RESTART` 標記的 `sigaction`。必須注意在 `sigchld_handler` 內使用迴圈，因為當多個子行程同時結束時，核心可能只會發送一次訊號。

### 5.2 Readline 顯示異常
在提示字元加入彩色 ANSI Code 後，按下 Backspace 會導致提示字元被部分刪除。
-   **根本原因**：Readline 無法正確計算彩色字串的實體顯示寬度。
-   **修復方式**：將所有非列印字元（色碼）包覆在 `\001` 與 `\002` (或 `RL_PROMPT_START_IGNORE`/`END`) 之間。

---

## 6. 總結與延伸建議

`fwsh` 透過精簡的 C 程式碼展示了作業系統與韌體工具的深度結合。它不只是一個文字介面，更是一個強大的底層除錯平台。

**未來延伸建議：**
1.  **實作工作控制 (Job Control)**：讓使用者能透過 `Ctrl+Z` 暫停工作，並用 `fg` 恢復，這需要更深度的 `tcsetpgrp` 終端機控制。
2.  **腳本解析引擎**：加入 `if` / `while` 等邏輯判斷，讓 `fwsh` 具備自動化測試能力。
3.  **底層硬體存取**：整合 `devmem` 功能，讓韌體工程師能直接透過 Shell 讀寫暫存器位址。
