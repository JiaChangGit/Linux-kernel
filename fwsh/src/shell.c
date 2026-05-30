/*
 * shell.c - Shell 核心流程
 *
 * 本檔負責互動式 Shell 的外層流程：
 *   1. 初始化 signal handler 與 Readline。
 *   2. 建立 prompt，讀取使用者輸入。
 *   3. 呼叫 parser 與 executor。
 *   4. 在離開前釋放 Shell 保存的 history。
 *
 * REPL = Read-Eval-Print Loop：
 *   Read   readline() 讀取一行輸入。
 *   Eval   parse_line() 解析，execute_pipeline() 執行。
 *   Print  各指令自行輸出結果。
 *   Loop   回到下一輪，直到 g_shell.running 變成 0。
 */

#include "shell.h"

#include <readline/history.h>  /* Readline history API */
#include <readline/readline.h> /* Readline 行編輯 API */

#include "executor.h"
#include "parser.h"

/* g_shell 的 extern 宣告在 shell.h；實體只定義一次。 */
ShellState g_shell = {.running = 0, .hist_count = 0, .hist_head = 0};

/* ── 訊號處理函式 ─────────────────────────────────── */

/*
 * sigchld_handler - 回收已結束的子行程
 *
 * 背景行程結束時，kernel 會送 SIGCHLD 給 Shell。
 * 若 parent 沒有 wait，child 會暫時停在 zombie 狀態。
 *
 * WNOHANG 讓 waitpid() 沒有可回收 child 時立刻回傳，
 * 不會卡住目前的 Shell。
 */
static void sigchld_handler(int sig) {
  (void)sig; /* handler 介面需要 sig，這裡不使用。 */
  /* 一次 signal 可能代表多個 child 已結束，因此用迴圈收乾淨。 */
  while (waitpid(-1, NULL, WNOHANG) > 0);
}

/*
 * sigint_handler - 處理 Ctrl+C
 *
 * 一般程式收到 SIGINT 會結束；互動式 Shell 不應直接退出。
 * 這裡的行為是換行、清掉目前輸入，並重新顯示 prompt。
 *
 * Readline 函式用途：
 *   rl_on_new_line()       告訴 Readline 目前游標在新的一行。
 *   rl_replace_line("", 0) 清空目前輸入緩衝區。
 *   rl_redisplay()         重繪 prompt 與輸入列。
 */
static void sigint_handler(int sig) {
  (void)sig;
  write(STDOUT_FILENO, "\n", 1); /* signal handler 中避免使用 printf()。 */
  rl_on_new_line();
  rl_replace_line("", 0);
  rl_redisplay();
}

/* ── 初始化 ───────────────────────────────────────── */

void shell_init(void) {
  /* SA_RESTART 讓部分被 signal 中斷的系統呼叫自動重試。 */
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;

  sa.sa_handler = sigchld_handler;
  sigaction(SIGCHLD, &sa, NULL);

  sa.sa_handler = sigint_handler;
  sigaction(SIGINT, &sa, NULL);

  /* 避免使用者按 Ctrl+Z 時把 Shell 自己暫停。 */
  sa.sa_handler = SIG_IGN;
  sigaction(SIGTSTP, &sa, NULL);

  g_shell.running = 1;

  /* 啟動時顯示簡短提示，讓使用者知道 help 可查看指令。 */
  printf(COLOR_GREEN
         "╔══════════════════════════════════════════╗\n"
         "║   fwsh %-6s  Firmware Mini Shell       ║\n"
         "║   Built for Firmware Engineers            ║\n"
         "║   Type 'help' to list all commands        ║\n"
         "╚══════════════════════════════════════════╝\n" COLOR_RESET,
         FWSH_VERSION);

  /* 使用 Readline 內建的 Tab 補全。 */
  rl_bind_key('\t', rl_complete);
}

/* ── REPL 主迴圈 ──────────────────────────────────── */

/*
 * build_prompt - 產生每一輪 REPL 使用的提示字元
 *
 * 顯示格式：
 *   [fwsh user@hostname ~/path]$
 *
 * 注意：
 *   ANSI 色碼本身不會顯示在畫面上。
 *   Readline 需要用 \001 和 \002 標記這些不可見字元，
 *   否則游標位置、Backspace、方向鍵顯示會錯亂。
 */
static void build_prompt(char* buf, size_t bufsz) {
  char hostname[64] = {0};
  char cwd[512] = {0};
  char display_cwd[512] = {0};

  gethostname(hostname, sizeof(hostname) - 1);

  if (getcwd(cwd, sizeof(cwd)) == NULL) strncpy(cwd, "?", 2);

  /* 若目前路徑位於 HOME 底下，用 ~ 縮短顯示。 */
  const char* home = getenv("HOME");
  if (home && strncmp(cwd, home, strlen(home)) == 0)
    snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(home));
  else
    strncpy(display_cwd, cwd, sizeof(display_cwd) - 1);

  snprintf(buf, bufsz,
           "\001" COLOR_GREEN
           "\002" /* Readline 不計算這段 ANSI 色碼的寬度。 */
           "[fwsh %s@%s %s]$ "
           "\001" COLOR_RESET "\002",
           getenv("USER") ? getenv("USER") : "user", hostname, display_cwd);
}

void shell_run(void) {
  char prompt[768];

  while (g_shell.running) {
    build_prompt(prompt, sizeof(prompt));

    /* readline() 負責行編輯、歷史鍵與 Tab 補全。 */
    char* line = readline(prompt);

    /* NULL 代表 EOF，通常是使用者按 Ctrl+D。 */
    if (line == NULL) {
      printf("\nexit\n");
      break;
    }

    /* 去掉開頭空白；若整行都是空白就進下一輪。 */
    char* trimmed = line;
    while (isspace((unsigned char)*trimmed)) trimmed++;
    if (*trimmed == '\0') {
      free(line);
      continue;
    }

    /* 交給 Readline 保存，支援上下鍵與 Ctrl+R 搜尋。 */
    add_history(trimmed);

    /* fwsh 自己也保存一份，供 history 內建指令顯示。 */
    int idx = g_shell.hist_head % MAX_HISTORY;
    free(g_shell.history[idx]);
    g_shell.history[idx] = strdup(trimmed);
    g_shell.hist_head++;
    if (g_shell.hist_count < MAX_HISTORY) g_shell.hist_count++;

    /* 每輪命令都建立一個暫時 Pipeline，執行後立即釋放。 */
    Pipeline pipeline;
    memset(&pipeline, 0, sizeof(pipeline));

    if (parse_line(trimmed, &pipeline) == 0) execute_pipeline(&pipeline);

    free_pipeline(&pipeline);
    free(line);
  }
}

/* ── 清理 ─────────────────────────────────────────── */

void shell_cleanup(void) {
  /* 釋放 fwsh 自己保存的 history 字串。 */
  for (int i = 0; i < MAX_HISTORY; i++) {
    free(g_shell.history[i]);
    g_shell.history[i] = NULL;
  }
  /* 釋放 Readline 內部保存的 history。 */
  rl_clear_history();
}
