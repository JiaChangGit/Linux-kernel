# fwsh (Firmware Mini Shell) API 技術報告

本報告針對 `/fwsh` 子專案進行深度 Codebase Trace 與架構分析。內容完全基於實體原始碼 (`src/*.c`) 與標頭檔 (`include/*.h`) 的實際實作。

---

## 第一階段：Codebase Trace (程式碼追蹤)

### 1. Project Structure (專案結構)

- **Source Files**:
    - `src/main.c`: 程式進入點與生命週期管理。
    - `src/shell.c`: REPL 主迴圈、訊號處理與提示符生成。
    - `src/parser.c`: 詞法分析與命令列解析。
    - `src/executor.c`: 執行引擎（Process/Pipe/Redirection 管理）。
    - `src/builtin.c`: 內建指令實作（包含韌體專屬工具）。
- **Header Files**:
    - `include/shell.h`: 全域型別定義與狀態。
    - `include/parser.h`, `include/executor.h`, `include/builtin.h`: 模組化介面定義。
- **Build System**:
    - **目前程式碼中未觀察到** `Makefile`。根據 `main.c` 註釋，使用 `gcc -lreadline` 進行連結。
- **Component Relationship**:
    `main` 驅動 `shell` 進入 REPL；`shell` 呼叫 `parser` 將字串轉換為 `Pipeline` 結構，再交由 `executor` 執行；`executor` 在分派時區分內建指令 (`builtin`) 與外部程式。

### 2. Semantic Element Extraction (語義要素萃取)

- **API**: `parse_line`, `execute_pipeline`, `exec_builtin`, `shell_init`, `shell_run`, `readline` (GNU Readline)。
- **Macros**: `MAX_PIPES` (16), `MAX_ARGS` (128), `MAX_HISTORY` (50), `_POSIX_C_SOURCE` (跨平台相容)。
- **Callbacks / Function Pointers**:
    - `BuiltinEntry`: 使用「名稱 → 函式指標」的 Dispatch Table 機制。
    - `sigaction`: 處理 `SIGCHLD` (非同步回收)、`SIGINT` (Ctrl+C 攔截)。
- **Memory Management**: 
    - 採用 `strdup` 動態配置字串。
    - `free_pipeline` 負責遞迴釋放整個 Pipeline 結構內的 heap 記憶體。
    - `g_shell.history` 使用環形緩衝區 (Circular Buffer) 管理歷史記錄。
- **Execution Model**: **Fork-Exec 模型**。支援多段 Pipe 串接與 I/O 重導向。

### 3. API / Macro Inventory

| 名稱 | 類型 | 呼叫位置 | 用途 | 影響 |
| :--- | :--- | :--- | :--- | :--- |
| `shell_init` | Function | `main.c:13` | 設定訊號處理器、初始化全域狀態。 | 決定了 Shell 對 Ctrl+C 與殭屍行程的反應行為。 |
| `parse_line` | Function | `shell.c:164` | 將原始字串解析為 `Pipeline`。 | 資料從「非結構化」轉為「結構化」的核心節點。 |
| `execute_pipeline` | Function | `shell.c:164` | 建立行程、Pipe 與重導向。 | 控制 Shell 的執行流與子行程拓樸。 |
| `BuiltinEntry` | Struct | `builtin.c:45` | 定義內建指令分派表。 | 簡化了新指令的擴充，無需修改分派邏輯。 |
| `SA_RESTART` | Flag | `shell.c:75` | 訊號旗標設定。 | 確保 Readline 等系統呼叫被訊號打斷後能自動重試。 |

### 4. Call Graph (呼叫圖譜)

- **Initialization Chain**:
    `main` -> `shell_init` (Signal setup) -> `shell_run`

- **REPL Loop (Runtime)**:
    `shell_run`
    -> `readline` (阻塞式等待輸入)
    -> `parse_line` (Lexer 掃描)
    -> `execute_pipeline`
        |-- `is_builtin` -> `exec_builtin` (直接執行)
        `-- `fork` -> `setup_redirections` -> `execvp` (外部執行)

- **Cleanup Chain**:
    `shell_run` 結束 -> `shell_cleanup` -> `free(g_shell.history)` -> `rl_clear_history`

### 5. Struct / Resource Tracing (資源追蹤)

- **`Pipeline` 結構**:
    - **定義**: `shell.h:60`
    - **Allocation**: 在 `shell_run` 的 stack 上配置。
    - **Ownership**: 內部的 `argv` 與 `in_file/out_file` 字串擁有權屬於 `Pipeline`，必須手動呼叫 `free_pipeline` 釋放。
- **`ShellState g_shell`**:
    - **狀態**: `running` 控制主迴圈開關。
    - **資源**: 擁有 `history[50]` 的字串指標，負責管理歷史記錄的存續。

### 6. Execution Trace (執行追蹤)

```text
[Signal Handling Flow]
SIGCHLD -> sigchld_handler -> while(waitpid(-1, ..., WNOHANG) > 0)
(確保背景行程結束後不遺留殭屍)

[Pipeline Execution Flow (以 A | B 為例)]
1. Parent: pipe(fds)
2. Parent: fork() -> Child A: dup2(fds[1], stdout) -> exec(A)
3. Parent: fork() -> Child B: dup2(fds[0], stdin)  -> exec(B)
4. Parent: close(fds[0]), close(fds[1]) (極其重要，否則 B 無法結束)
5. Parent: waitpid(B) (等待最後一段結束)
```

---

## 第二階段：Architecture / API Technical Report

### 1. Execution Semantics & Process Topology (執行語義與拓樸)

`fwsh` 的執行核心在於其對 **POSIX Process Model** 的精準控制：
- **內建與外部的分流**：單一指令且非背景時，`execute_pipeline` 優先進行內建指令檢查。這是為了支援 `cd` (切換 Shell 工作目錄) 與 `exit` (修改 Shell `running` 狀態)，因為 `fork` 出來的子行程無法修改父行程的環境。
- **管線串聯機制**：採用「預先建立所有管線 (`pipe`)」隨後「一次性 Fork 所有子行程」的策略。
    - **技術細節**：在子行程中，`execute_pipeline` 會主動關閉**所有**不屬於該行程的管線端點 (`close(pipes[j][0])` 等)。這保證了寫端被正確關閉，從而使讀端能收到 EOF，避免管線阻塞 (Deadlock)。

### 2. Semantic Analysis of Parser (解析器語義分析)

`parser.c` 實作了一個具備**引號感知 (Quote-aware)** 的詞法分析器：
- **引號語義**：單引號 (`'`) 內視為純文字；雙引號 (`"`) 支援反斜線轉義 (`\"`, `\\`)。
- **遞增建構**：解析器邊讀取 Token 邊填充 `Cmd` 結構。若遇到 `|`，則遞增 `cmd_idx` 並移動到下一個 `Cmd` 插槽。
- **記憶體策略**：採用「全面副本 (Full Copy)」模式。解析過程中產生的所有詞彙皆透過 `strdup` 轉移至 Heap，確保 `Pipeline` 結構的生命週期獨立於輸入緩衝區。

### 3. Built-in Command Architecture (內建指令架構)

`builtin.c` 展示了高度解耦的**分派表 (Dispatch Table)** 設計：
- **擴充性**：新增指令僅需實作對應函式並向 `builtins[]` 陣列註冊。
- **韌體工具整合**：
    - `hexdump`：手動實作檔案偏移與 Hex/ASCII 雙欄顯示，不依賴系統工具。
    - `crc32`：實作了 **IEEE 802.3 查找表法**。這是在受限環境中快速驗證資料完整性的典型實作。
    - `memmap`：透過解析 `/proc/iomem` 展示實體記憶體佈局。

### 4. Lifecycle & Signal Safety (生命週期與訊號安全)

- **Signal Masking**：`shell_init` 使用 `sigaction` 取代舊式的 `signal`。這提供了更好的跨平台行為一致性（如 `SA_RESTART`）。
- **Async-Signal-Safety**：`sigint_handler` 中正確使用了 `write()` 而非 `printf()`。因為 `printf` 非執行緒安全且非訊號安全（內部使用緩衝區且可能呼叫 `malloc`），在訊號處理中呼叫可能導致死鎖。

### 5. Potential Bug & Risk (潛在風險)

- **Buffer Overflow**：`read_word` 雖然使用了 `bufsz` 限制，但全域 `MAX_INPUT` 若與實際輸入長度不一致可能存在風險（雖目前實作尚稱安全）。
- **Waitpid Limitation**：目前的 `execute_pipeline` 僅在前景模式下等待所有子行程。對於背景執行 (`&`)，僅印出 PID。若背景行程極多，可能會暫時佔用過多系統資源。
- **Pipe Limit**：`MAX_PIPES` 固定為 16。對於極端複雜的自動化腳本可能不足，但在交互式 Shell 中已足夠。

---
**結論**：`fwsh` 是一個設計嚴謹、模組化程度極高的 Shell 實作。其在處理管線同步、訊號安全以及為韌體工程師量身定做工具組方面的表現，使其超越了普通的練習專案，具備了作為小型嵌入式系統診斷殼層的潛力。
檔案分析時間：2026-05-17
分析者：Gemini CLI
