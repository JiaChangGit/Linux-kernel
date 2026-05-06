#ifndef SHELL_H
#define SHELL_H

/*
 * shell.h — fwsh 核心標頭檔
 * 定義共用資料型別、常數、全域 Shell 狀態
 *
 * 設計思路：
 *   - Pipeline 是最高層的執行單位（一行輸入）
 *   - Pipeline 由一個或多個 Cmd 組成（以 | 分隔）
 *   - 每個 Cmd 有自己的 argv、I/O 重導向檔案
 */
// man strdup _POSIX_C_SOURCE >= 200809L
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ── 版本與樣式 ───────────────────────────────────────── */
#define FWSH_VERSION "1.0.0"
#define COLOR_GREEN "\033[1;32m"   /* 粗體綠色，用於提示符與標題 */
#define COLOR_CYAN "\033[1;36m"    /* 粗體青色                    */
#define COLOR_YELLOW "\033[1;33m"  /* 粗體黃色                    */
#define COLOR_MAGENTA "\033[1;35m" /* 粗體洋紅                    */
#define COLOR_RESET "\033[0m"      /* 恢復預設顏色                 */

/* ── 大小限制 ─────────────────────────────────────────── */
#define MAX_INPUT 2048 /* 單行輸入最大字元數                */
#define MAX_ARGS 128   /* 單一指令最多引數數量              */
#define MAX_HISTORY 50 /* 歷史記錄保留筆數（環形緩衝區）    */
#define MAX_PIPES 16   /* Pipeline 中最多 Cmd 段數          */

/* ── 單一指令結構 ─────────────────────────────────────── */
/*
 * 一個 Cmd 代表 pipeline 中的一段，例如：
 *   cat file.bin | hexdump | grep "FF"
 * 有三個 Cmd：cat、hexdump、grep
 */
typedef struct {
  char* argv[MAX_ARGS]; /* NULL 結尾的引數陣列，argv[0] 為指令名稱 */
  int argc;             /* 實際引數數量                             */
  char* in_file;        /* 輸入重導向檔名（< file），NULL 表示無    */
  char* out_file;       /* 輸出重導向檔名（> 或 >>），NULL 表示無   */
  int out_append;       /* 1 = 追加模式 (>>)，0 = 覆寫模式 (>)    */
} Cmd;

/* ── Pipeline 結構 ────────────────────────────────────── */
typedef struct {
  Cmd cmds[MAX_PIPES]; /* 各段指令陣列                */
  int ncmds;           /* 有效指令數量                 */
  int background;      /* 1 = 背景執行 (&)，0 = 前景  */
} Pipeline;

/* ── Shell 全域狀態 ───────────────────────────────────── */
/*
 * 使用單一全域結構集中管理 Shell 狀態，方便各模組存取。
 * 宣告為 extern，實際定義在 shell.c 中。
 */
typedef struct {
  char* history[MAX_HISTORY]; /* 環形緩衝區儲存歷史指令    */
  int hist_count;             /* 目前已儲存的歷史筆數       */
  int hist_head;              /* 環形緩衝區寫入指標         */
  int running;                /* 主迴圈旗標，0 則退出       */
} ShellState;

extern ShellState g_shell;

/* ── 函式原型 ─────────────────────────────────────────── */
void shell_init(void);
void shell_run(void);
void shell_cleanup(void);

#endif /* SHELL_H */
