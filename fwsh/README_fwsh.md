# fwsh — Firmware Mini Shell

> 一個以韌體工程師視角設計的迷你 Shell，展示 C 系統程式設計能力。

## 功能特色

- **完整 REPL 迴圈**：GNU Readline 支援行編輯、上下鍵歷史、Ctrl+R 反向搜尋、Tab 自動補全
- **Pipeline 支援**：多段管線 `|`、I/O 重導向 `< > >>`、背景執行 `&`
- **訊號處理**：SIGCHLD 自動回收殭屍行程，SIGINT 不殺死 Shell
- **韌體工程師專屬工具**：
  - `hexdump` — Hex + ASCII 雙欄顯示二進位檔案
  - `crc32` — IEEE 802.3 CRC-32 檢查碼計算
  - `memmap` — 顯示 /proc/iomem 實體記憶體佈局

## 快速開始

```bash
# 安裝依賴
sudo apt install -y build-essential libreadline-dev

# 編譯
make

# 執行
./fwsh
```

## 專案架構

```
fwsh/
├── Makefile          建置系統
├── README.md         本文件
├── include/
│   ├── shell.h       核心型別定義與全域狀態
│   ├── parser.h      解析器介面
│   ├── executor.h    執行器介面
│   └── builtin.h     內建指令介面
└── src/
    ├── main.c        程式入口點
    ├── shell.c       REPL 主迴圈、初始化、訊號處理
    ├── parser.c      命令列解析器
    ├── executor.c    Pipeline 執行引擎（fork/exec/pipe/dup2）
    └── builtin.c     所有內建指令實作
```

## 指令列表

| 指令 | 說明 |
|------|------|
| `cd [dir]` | 切換目錄，支援 `~` 和 `-` |
| `pwd` | 顯示目前目錄 |
| `history` | 顯示指令歷史 |
| `clear` | 清除畫面 |
| `help` | 顯示說明 |
| `exit [code]` | 離開 Shell |
| `hexdump <file> [len]` | Hex dump 二進位檔案 |
| `crc32 <file>` | 計算 CRC-32 檢查碼 |
| `memmap` | 顯示實體記憶體配置 |

## 技術亮點

- **零動態依賴**：只依賴 GNU Readline，其餘純 POSIX API
- **記憶體安全**：全程配對 malloc/free，可用 valgrind 驗證無洩漏
- **函式指標分派表**：內建指令採命令表架構，易於擴充
- **正確的管線 fd 管理**：父子行程均正確關閉不需要的 pipe fd
