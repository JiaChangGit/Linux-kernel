/*
 * shell.c — Shell 核心：初始化、REPL 主迴圈、訊號處理
 *
 * REPL = Read → Eval → Print → Loop
 *   Read  : readline() 讀取一行輸入（含行編輯、歷史記錄鍵）
 *   Eval  : parse_line() + execute_pipeline()
 *   Print : 結果輸出已在各指令內部完成
 *   Loop  : 回到 Read，直到 g_shell.running == 0
 *
 * 訊號處理設計：
 *   SIGCHLD — 非同步回收背景子行程的殭屍（WNOHANG）
 *   SIGINT  — Ctrl+C 不殺死 Shell，只清除當前輸入行
 *   SIGTSTP — 忽略（Shell 不應被 Ctrl+Z 暫停）
 */

#include "shell.h"

#include <readline/history.h>  /* Readline 內建歷史記錄       */
#include <readline/readline.h> /* GNU Readline 提供行編輯功能 */

#include "executor.h"
#include "parser.h"

/* ── 全域狀態實體定義（extern 宣告在 shell.h） ─────── */
ShellState g_shell = {.running = 0, .hist_count = 0, .hist_head = 0};

/* ── 訊號處理函式 ─────────────────────────────────── */

/*
 * sigchld_handler - 非同步回收殭屍行程
 *
 * 當背景子行程結束時，核心會發送 SIGCHLD 給父行程（Shell）。
 * 若不處理，子行程會成為「殭屍行程」佔用 PID 表。
 * WNOHANG 確保此函式不會阻塞，立即返回。
 */
static void sigchld_handler(int sig) {
  (void)sig; /* 消除未使用警告 */
  /* 持續回收所有已結束的子行程 */
  while (waitpid(-1, NULL, WNOHANG) > 0);
}

/*
 * sigint_handler - 處理 Ctrl+C
 *
 * 一般程式收到 SIGINT 會結束，但 Shell 不能這樣做。
 * 正確行為：清除當前輸入行，重新顯示提示符。
 *
 * rl_on_new_line()     告訴 readline 目前在新行
 * rl_replace_line("",0) 清除輸入緩衝區
 * rl_redisplay()       重繪提示符與當前行
 */
static void sigint_handler(int sig) {
  (void)sig;
  write(STDOUT_FILENO, "\n", 1); /* 在訊號處理中用 write()，非 printf() */
  rl_on_new_line();
  rl_replace_line("", 0);
  rl_redisplay();
}

/* ── 初始化 ───────────────────────────────────────── */

void shell_init(void) {
  /* 設定訊號處理：SA_RESTART 讓被訊號打斷的 syscall 自動重試 */
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;

  sa.sa_handler = sigchld_handler;
  sigaction(SIGCHLD, &sa, NULL);

  sa.sa_handler = sigint_handler;
  sigaction(SIGINT, &sa, NULL);

  /* 忽略 SIGTSTP（Shell 自身不可被 Ctrl+Z 暫停） */
  sa.sa_handler = SIG_IGN;
  sigaction(SIGTSTP, &sa, NULL);

  g_shell.running = 1;

  /* 啟動 Banner */
  printf(COLOR_GREEN
         "╔══════════════════════════════════════════╗\n"
         "║   fwsh %-6s  Firmware Mini Shell       ║\n"
         "║   Built for Firmware Engineers            ║\n"
         "║   Type 'help' to list all commands        ║\n"
         "╚══════════════════════════════════════════╝\n" COLOR_RESET,
         FWSH_VERSION);

  /* 啟用 Readline Tab 自動補全 */
  rl_bind_key('\t', rl_complete);
}

/* ── REPL 主迴圈 ──────────────────────────────────── */

/*
 * build_prompt - 動態產生提示符字串
 *
 * 格式：[fwsh user@hostname ~/path]$
 *
 * Readline 提示符中嵌入 ANSI 色碼時，不可見字元必須包在
 * \001...\002 之間，否則 Readline 計算行寬時會出錯，
 * 導致行編輯（Backspace / 方向鍵）顯示異常。
 */
static void build_prompt(char* buf, size_t bufsz) {
  char hostname[64] = {0};
  char cwd[512] = {0};
  char display_cwd[512] = {0};

  gethostname(hostname, sizeof(hostname) - 1);

  if (getcwd(cwd, sizeof(cwd)) == NULL) strncpy(cwd, "?", 2);

  /* 以 ~ 縮寫家目錄前置路徑 */
  const char* home = getenv("HOME");
  if (home && strncmp(cwd, home, strlen(home)) == 0)
    snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(home));
  else
    strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);

  snprintf(buf, bufsz,
           "\001" COLOR_GREEN
           "\002" /* \001..\002 = 不可見字元邊界 */
           "[fwsh %s@%s %s]$ "
           "\001" COLOR_RESET "\002",
           getenv("USER") ? getenv("USER") : "user", hostname, display_cwd);
}

void shell_run(void) {
  char prompt[768];

  while (g_shell.running) {
    build_prompt(prompt, sizeof(prompt));

    /* readline() 處理行編輯、上下鍵歷史、Tab 補全 */
    char* line = readline(prompt);

    /* NULL 表示 EOF（使用者按 Ctrl+D）*/
    if (line == NULL) {
      printf("\nexit\n");
      break;
    }

    /* 略過全空白行 */
    char* trimmed = line;
    while (isspace((unsigned char)*trimmed)) trimmed++;
    if (*trimmed == '\0') {
      free(line);
      continue;
    }

    /* 加入 Readline 內建歷史（支援 Ctrl+R 反向搜尋） */
    add_history(trimmed);

    /* 同時存入 fwsh 自己的環形歷史緩衝區 */
    int idx = g_shell.hist_head % MAX_HISTORY;
    free(g_shell.history[idx]);
    g_shell.history[idx] = strdup(trimmed);
    g_shell.hist_head++;
    if (g_shell.hist_count < MAX_HISTORY) g_shell.hist_count++;

    /* 解析 → 執行 → 釋放 */
    Pipeline pipeline;
    memset(&pipeline, 0, sizeof(pipeline));

    if (parse_line(trimmed, &pipeline) == 0) execute_pipeline(&pipeline);

    free_pipeline(&pipeline);
    free(line);
  }
}

/* ── 清理 ─────────────────────────────────────────── */

void shell_cleanup(void) {
  /* 釋放歷史記錄緩衝區 */
  for (int i = 0; i < MAX_HISTORY; i++) {
    free(g_shell.history[i]);
    g_shell.history[i] = NULL;
  }
  /* 清除 Readline 的歷史記憶體 */
  rl_clear_history();
}
