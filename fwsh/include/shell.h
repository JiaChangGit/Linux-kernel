#ifndef SHELL_H
#define SHELL_H

/*
 * shell.h - fwsh 共用定義
 *
 * 本檔集中放跨模組共用的常數、資料結構與 Shell 狀態。
 * 主要資料關係：
 *   - 一行輸入會被解析成一個 Pipeline。
 *   - Pipeline 由多個 Cmd 組成，Cmd 之間用 | 串接。
 *   - 每個 Cmd 保存自己的 argv、輸入重導向與輸出重導向設定。
 */
/* 讓 POSIX 函式宣告可見，例如 strdup()。 */
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

/* ── 版本與終端機樣式 ─────────────────────────────────── */
#define FWSH_VERSION "1.0.0"
#define COLOR_GREEN "\033[1;32m"   /* 粗體綠色，提示字元與標題使用 */
#define COLOR_CYAN "\033[1;36m"    /* 粗體青色 */
#define COLOR_YELLOW "\033[1;33m"  /* 粗體黃色 */
#define COLOR_MAGENTA "\033[1;35m" /* 粗體洋紅色 */
#define COLOR_RESET "\033[0m"      /* 還原終端機預設顏色 */

/* ── 固定大小限制 ─────────────────────────────────────── */
#define MAX_INPUT 2048 /* 單行輸入最多字元數 */
#define MAX_ARGS 128   /* 單一 Cmd 最多 argv 數量 */
#define MAX_HISTORY 50 /* fwsh 自行保存的歷史紀錄筆數 */
#define MAX_PIPES 16   /* 一個 Pipeline 最多 Cmd 段數 */

/* ── 單一指令結構 ─────────────────────────────────────── */
/*
 * Cmd 代表 pipeline 中的一段指令。
 * 例：
 *   cat file.bin | hexdump | grep "FF"
 * 會被解析成三個 Cmd：cat、hexdump、grep。
 */
typedef struct {
  char* argv[MAX_ARGS]; /* NULL 結尾的引數陣列；argv[0] 是指令名稱 */
  int argc;             /* argv 中實際有幾個引數 */
  char* in_file;        /* < file 的檔名；NULL 表示不重導向 stdin */
  char* out_file;       /* > 或 >> 的檔名；NULL 表示不重導向 stdout */
  int out_append;       /* 1 表示 >> 追加；0 表示 > 覆寫 */
} Cmd;

/* ── Pipeline 結構 ────────────────────────────────────── */
typedef struct {
  Cmd cmds[MAX_PIPES]; /* 依序保存 pipeline 中的 Cmd */
  int ncmds;           /* 有效 Cmd 數量 */
  int background;      /* 1 表示背景執行 (&)；0 表示前景執行 */
} Pipeline;

/* ── Shell 全域狀態 ───────────────────────────────────── */
/*
 * ShellState 保存跨命令仍需保留的狀態。
 * 實體定義在 shell.c；其他模組透過 extern 使用。
 */
typedef struct {
  char* history[MAX_HISTORY]; /* 環形緩衝區，保存最近輸入的命令 */
  int hist_count;             /* 目前可顯示的 history 筆數 */
  int hist_head;              /* 下一筆 history 的寫入位置 */
  int running;                /* REPL 執行旗標；0 表示結束 Shell */
} ShellState;

extern ShellState g_shell;

/* ── Shell 生命週期 API ───────────────────────────────── */
void shell_init(void);
void shell_run(void);
void shell_cleanup(void);

#endif /* SHELL_H */
