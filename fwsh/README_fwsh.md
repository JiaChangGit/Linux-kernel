# fwsh — Firmware Mini Shell

> A lightweight shell designed from a firmware engineer’s perspective, demonstrating advanced C system programming capabilities.

---

# Features

## Interactive REPL Environment

- GNU Readline integration
- Command-line editing
- Command history navigation
- `Ctrl + R` reverse search
- `Tab` auto-completion

---

## Shell Execution Features

- Multi-stage pipeline support: `|`
- I/O redirection:
  - input `<`
  - output `>`
  - append `>>`
- Background execution: `&`

---

## Signal Handling

- Automatic zombie process cleanup via `SIGCHLD`
- `SIGINT` (`Ctrl + C`) does not terminate the shell itself

---

## Firmware Engineer Utilities

### `hexdump`

Display binary files in:

- Hexadecimal view
- ASCII view

dual-column format.

---

### `crc32`

Calculate IEEE 802.3 CRC-32 checksum.

---

### `memmap`

Display physical memory layout using:

```bash
/proc/iomem
```

---

# Quick Start

## Install Dependencies

```bash
sudo apt install -y \
    build-essential \
    libreadline-dev
```

---

## Build

```bash
make
```

---

## Run

```bash
./fwsh
```

---

# Project Structure

```text
fwsh/
├── docs/
│   ├── ......
│
├── Makefile
│   # Build system
│
├── README_fwsh.md
│
├── include/
│   ├── shell.h
│   │   # Core types and global states
│   │
│   ├── parser.h
│   │   # Parser interface
│   │
│   ├── executor.h
│   │   # Execution engine interface
│   │
│   └── builtin.h
│       # Built-in command interface
│
└── src/
    ├── main.c
    │   # Program entry point
    │
    ├── shell.c
    │   # REPL loop
    │   # Initialization
    │   # Signal handling
    │
    ├── parser.c
    │   # Command line parser
    │
    ├── executor.c
    │   # Pipeline execution engine
    │   # fork / exec / pipe / dup2
    │
    └── builtin.c
        # Built-in command implementations
```

---

# Built-in Commands

| Command | Description |
|---|---|
| `cd [dir]` | Change directory, supports `~` and `-` |
| `pwd` | Print current working directory |
| `history` | Show command history |
| `clear` | Clear terminal screen |
| `help` | Show help information |
| `exit [code]` | Exit shell |
| `hexdump <file> [len]` | Dump binary file in hex format |
| `crc32 <file>` | Calculate CRC-32 checksum |
| `memmap` | Display physical memory layout |

---

# Technical Highlights

## Minimal Dependencies

- Only depends on:
  - GNU Readline
- Everything else uses:
  - pure POSIX APIs

---

## Memory Safety

- Proper `malloc/free` pairing
- Can be verified using:

```bash
valgrind
```

with zero memory leaks.

---

## Function Pointer Dispatch Table

Built-in commands use:

- command dispatch tables
- function pointer architecture

making the shell highly extensible.

---

## Correct Pipeline File Descriptor Management

Proper pipe fd lifecycle management across:

- parent processes
- child processes

preventing:

- fd leaks
- deadlocks
- hanging pipelines

---

---

# fwsh — 韌體工程迷你 Shell

> 一個以韌體工程師視角設計的迷你 Shell，展示進階 C 系統程式設計能力。

---

# 功能特色

## 完整互動式 REPL 環境

- GNU Readline 支援
- 指令列編輯
- 歷史紀錄瀏覽
- `Ctrl + R` 反向搜尋
- `Tab` 自動補全

---

## Shell 執行功能

- 多段 Pipeline：`|`
- I/O 重導向：
  - 輸入 `<`
  - 輸出 `>`
  - 附加 `>>`
- 背景執行：`&`

---

## 訊號處理

- 使用 `SIGCHLD` 自動回收 zombie process
- `SIGINT`（`Ctrl + C`）不會直接殺死 Shell

---

## 韌體工具

### `hexdump`

以：

- Hexadecimal
- ASCII

雙欄格式顯示二進位檔案。

---

### `crc32`

計算 IEEE 802.3 CRC-32 檢查碼。

---

### `memmap`

透過：

```bash
/proc/iomem
```

顯示實體記憶體配置。

---

# 快速開始

## 安裝依賴套件

```bash
sudo apt install -y \
    build-essential \
    libreadline-dev
```

---

## 編譯

```bash
make
```

---

## 執行

```bash
./fwsh
```

---

# 專案架構

```text
fwsh/
├── docs/
│   ├── ......
│
├── Makefile
│   # 建置系統
│
├── README_fwsh.md
│
├── include/
│   ├── shell.h
│   │   # 核心型別與全域狀態
│   │
│   ├── parser.h
│   │   # Parser 介面
│   │
│   ├── executor.h
│   │   # 執行器介面
│   │
│   └── builtin.h
│       # 內建指令介面
│
└── src/
    ├── main.c
    │   # 程式入口
    │
    ├── shell.c
    │   # REPL 主迴圈
    │   # 初始化
    │   # 訊號處理
    │
    ├── parser.c
    │   # 命令列解析器
    │
    ├── executor.c
    │   # Pipeline 執行引擎
    │   # fork / exec / pipe / dup2
    │
    └── builtin.c
        # 所有內建指令實作
```

---

# 內建指令列表

| 指令 | 說明 |
|---|---|
| `cd [dir]` | 切換目錄，支援 `~` 與 `-` |
| `pwd` | 顯示目前工作目錄 |
| `history` | 顯示歷史紀錄 |
| `clear` | 清除終端畫面 |
| `help` | 顯示說明資訊 |
| `exit [code]` | 離開 Shell |
| `hexdump <file> [len]` | 顯示 binary hex dump |
| `crc32 <file>` | 計算 CRC-32 檢查碼 |
| `memmap` | 顯示實體記憶體配置 |

---

# 技術亮點

## 最小依賴設計

- 僅依賴：
  - GNU Readline
- 其餘完全使用：
  - POSIX API

---

## 記憶體安全

- 所有 `malloc/free` 成對管理
- 可透過：

```bash
valgrind
```

驗證無 memory leak。

---

## 函式指標分派表

內建指令採用：

- command dispatch table
- function pointer architecture

設計，方便後續擴充。

---

## 正確的 Pipeline fd 管理

完整管理：

- parent process
- child process

中的 pipe fd 生命周期，避免：

- fd leak
- deadlock
- pipeline 卡死
