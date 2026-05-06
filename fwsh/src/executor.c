/*
 * executor.c — Pipeline 執行引擎
 *
 * 這是 Shell 中技術含量最高的模組，涉及以下 POSIX 系統呼叫：
 *   fork()   — 建立子行程
 *   execvp() — 在子行程中載入新程式（取代目前行程映像）
 *   pipe()   — 建立單向資料管線（一對 fd：讀端 + 寫端）
 *   dup2()   — 複製 fd 到指定編號（用於把 pipe 接到 stdin/stdout）
 *   waitpid()— 等待特定子行程結束並取得結束碼
 *
 * Pipeline 執行流程圖（以 "A | B | C" 為例）：
 *
 *   pipe[0]        pipe[1]
 *   +-------+      +-------+
 *   | r | w |      | r | w |
 *   +---+---+      +---+---+
 *       ↑               ↑
 *  [A stdout]      [B stdout]
 *  [B stdin ]      [C stdin ]
 *
 *  Parent 建立 pipe[0] 和 pipe[1]，然後 fork 三個子行程：
 *    A：stdout → pipe[0][1]（寫入 pipe[0]）
 *    B：stdin  ← pipe[0][0]（從 pipe[0] 讀取）
 *       stdout → pipe[1][1]（寫入 pipe[1]）
 *    C：stdin  ← pipe[1][0]（從 pipe[1] 讀取）
 *
 *  關鍵：Parent 最後必須關閉所有 pipe fd，
 *        否則 B 和 C 的 stdin 永遠不會收到 EOF，會無限等待。
 */

#include "executor.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "builtin.h"

/* ── 子行程中的 I/O 重導向設定 ─────────────────────── */

/*
 * setup_redirections - 在子行程中設定 I/O 重導向
 *
 * 必須在 execvp() 之前、fork() 之後呼叫（僅在子行程中）。
 *
 * dup2(oldfd, newfd)：
 *   複製 oldfd 到 newfd，關閉原本的 newfd。
 *   效果：之後對 newfd 的讀寫，實際上操作的是 oldfd 指向的資源。
 *
 * 例如 dup2(fd, STDIN_FILENO)：
 *   將 fd（一個 open() 開啟的檔案）複製到 fd=0（stdin），
 *   程式讀 stdin 時實際上就是在讀那個檔案。
 */
static int setup_redirections(Cmd* cmd) {
  /* 輸入重導向：< file */
  if (cmd->in_file) {
    int fd = open(cmd->in_file, O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "fwsh: %s: %s\n", cmd->in_file, strerror(errno));
      return -1;
    }
    dup2(fd, STDIN_FILENO);
    close(fd); /* dup2 後原始 fd 不再需要，立即關閉以免 fd leak */
  }

  /* 輸出重導向：> file 或 >> file */
  if (cmd->out_file) {
    int flags = O_WRONLY | O_CREAT | (cmd->out_append ? O_APPEND : O_TRUNC);
    int fd = open(cmd->out_file, flags, 0644);
    if (fd < 0) {
      fprintf(stderr, "fwsh: %s: %s\n", cmd->out_file, strerror(errno));
      return -1;
    }
    dup2(fd, STDOUT_FILENO);
    close(fd);
  }

  return 0;
}

/* ── 執行單一外部指令（子行程中呼叫）──────────────── */

static void exec_external(Cmd* cmd) {
  if (setup_redirections(cmd) < 0) _exit(1);

  /*
   * execvp(file, argv)：
   *   - 在 PATH 中搜尋 file（有 'p' 的版本）
   *   - argv 必須是 NULL 結尾的字串陣列
   *   - 成功後此行程映像被完全替換，execvp 不會返回
   *   - 失敗才返回（例如指令不存在）
   */
  execvp(cmd->argv[0], cmd->argv);

  /* 只有 execvp 失敗才會執行到這裡 */
  fprintf(stderr, "fwsh: %s: %s\n", cmd->argv[0], strerror(errno));
  _exit(127); /* 127 是 POSIX 慣例：command not found */
}

/* ── 主執行函式 ──────────────────────────────────── */

int execute_pipeline(Pipeline* pipeline) {
  if (pipeline->ncmds == 0) return 0;

  /*
   * 最常見的情況：單一指令，非背景執行。
   * 先檢查是否為內建指令 → 直接在 Shell 行程執行，不 fork。
   * 這樣 cd、exit 等指令才能修改 Shell 自身的狀態。
   */
  if (pipeline->ncmds == 1 && !pipeline->background) {
    Cmd* cmd = &pipeline->cmds[0];
    if (cmd->argc == 0) return 0;
    if (is_builtin(cmd->argv[0])) return exec_builtin(cmd);
  }

  /*
   * 多段 Pipeline 或背景執行：需要 fork。
   *
   * 建立 (ncmds - 1) 個管線。
   * pipes[i][0] = pipe[i] 讀端
   * pipes[i][1] = pipe[i] 寫端
   * cmd[i] 的 stdout → pipes[i][1]
   * cmd[i+1] 的 stdin ← pipes[i][0]
   */
  int npipes = pipeline->ncmds - 1;
  int pipes[MAX_PIPES][2];
  pid_t pids[MAX_PIPES];
  int nforked = 0;

  /* 預先建立所有管線 */
  for (int i = 0; i < npipes; i++) {
    if (pipe(pipes[i]) < 0) {
      perror("fwsh: pipe");
      /* 清理已建立的管線 */
      for (int j = 0; j < i; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      return -1;
    }
  }

  /* 對每段 Cmd fork 一個子行程 */
  for (int i = 0; i < pipeline->ncmds; i++) {
    Cmd* cmd = &pipeline->cmds[i];
    if (cmd->argc == 0) continue;

    pids[nforked] = fork();

    if (pids[nforked] < 0) {
      perror("fwsh: fork");
      break;
    }

    if (pids[nforked] == 0) {
      /*
       * ═══ 子行程 ═══
       *
       * 第 i 段 Cmd 的 I/O 接線：
       *   - 若 i > 0：stdin ← pipes[i-1][0]（從前一段讀）
       *   - 若 i < npipes：stdout → pipes[i][1]（寫到下一段）
       *
       * 接好後，關閉子行程中所有 pipe fd（非常重要！）
       * 若不關閉，即使父行程關閉了，寫端仍被子行程持有，
       * 導致讀端永遠收不到 EOF。
       */

      /* 接管線輸入（非第一段） */
      if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);

      /* 接管線輸出（非最後一段） */
      if (i < npipes) dup2(pipes[i][1], STDOUT_FILENO);

      /* 關閉子行程中的所有 pipe fd */
      for (int j = 0; j < npipes; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }

      /*
       * 管線中間段的內建指令（如 echo | grep）：
       * 雖然是內建，但在管線中必須 fork，所以這裡
       * 執行完後用 _exit() 結束子行程。
       */
      if (is_builtin(cmd->argv[0])) _exit(exec_builtin(cmd));

      exec_external(cmd);
      _exit(127); /* 不會執行到這裡 */
    }

    /* 父行程繼續 fork 下一段 */
    nforked++;
  }

  /*
   * ═══ 父行程 ═══
   * 關閉所有 pipe fd。
   * 若不關閉，子行程的讀端永遠不會收到 EOF（因為父行程持有寫端）。
   */
  for (int i = 0; i < npipes; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }

  /* 等待前景子行程結束 */
  int last_status = 0;

  if (!pipeline->background) {
    for (int i = 0; i < nforked; i++) {
      int status;
      waitpid(pids[i], &status, 0);
      /* 只記錄最後一段的結束碼（符合 POSIX Pipeline 語義） */
      if (i == nforked - 1)
        last_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
  } else {
    /* 背景執行：不等待，只印出 PID 供使用者追蹤 */
    printf("[background]");
    for (int i = 0; i < nforked; i++) printf(" %d", pids[i]);
    printf("\n");
  }

  return last_status;
}
