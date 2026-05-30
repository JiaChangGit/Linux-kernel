/*
 * executor.c - Pipeline 執行引擎
 *
 * 本模組把 parser 產生的 Pipeline 轉成真正的 process 與 fd 接線。
 * 主要使用的 POSIX API：
 *   fork()     建立子行程。
 *   execvp()   在子行程中載入外部程式。
 *   pipe()     建立單向資料通道。
 *   dup2()     把 pipe 或檔案接到 stdin/stdout。
 *   waitpid()  等待或回收子行程。
 *
 * 以 "A | B | C" 為例：
 *
 *   pipe[0]        pipe[1]
 *   +-------+      +-------+
 *   | r | w |      | r | w |
 *   +---+---+      +---+---+
 *       ↑               ↑
 *  [A stdout]      [B stdout]
 *  [B stdin ]      [C stdin ]
 *
 * Parent 建立 pipe[0] 和 pipe[1]，再 fork 三個 child：
 *   A stdout -> pipe[0][1]
 *   B stdin  <- pipe[0][0]
 *   B stdout -> pipe[1][1]
 *   C stdin  <- pipe[1][0]
 *
 * 關鍵：
 *   parent 和 child 都要關閉不需要的 pipe fd。
 *   只要還有人持有寫端，讀端就可能收不到 EOF。
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

/* ── 子行程中的 I/O 重導向 ─────────────────────────── */

/*
 * setup_redirections - 將 Cmd 的重導向設定套到目前 process
 *
 * 呼叫時機：
 *   fork() 之後、execvp() 之前，且只在 child 中呼叫。
 *
 * dup2(oldfd, newfd) 會讓 newfd 指向 oldfd 的同一個 I/O 目標。
 * 例：dup2(fd, STDIN_FILENO) 後，程式讀 stdin 就是在讀 fd。
 *
 * 這裡不處理 built-in 的 parent redirection；目前只服務外部指令路徑。
 */
static int setup_redirections(Cmd* cmd) {
  /* < file：把檔案接到 stdin。 */
  if (cmd->in_file) {
    int fd = open(cmd->in_file, O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "fwsh: %s: %s\n", cmd->in_file, strerror(errno));
      return -1;
    }
    dup2(fd, STDIN_FILENO);
    close(fd); /* dup2 後已可透過 stdin 使用，原 fd 立即關閉。 */
  }

  /* > file 或 >> file：把 stdout 接到檔案。 */
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

/* ── 外部指令執行路徑，只在 child 中呼叫 ───────────── */

static void exec_external(Cmd* cmd) {
  if (setup_redirections(cmd) < 0) _exit(1);

  /*
   * execvp(file, argv) 會依 PATH 搜尋 file。
   * 成功後 child 的程式映像被替換，不會回到這份程式碼。
   * 只有失敗時才會繼續往下執行。
   */
  execvp(cmd->argv[0], cmd->argv);

  /* 只有 execvp() 失敗才會執行到這裡。 */
  fprintf(stderr, "fwsh: %s: %s\n", cmd->argv[0], strerror(errno));
  _exit(127); /* 127 是 shell 常見的 command not found 狀態碼。 */
}

/* ── 主執行流程 ──────────────────────────────────── */

int execute_pipeline(Pipeline* pipeline) {
  if (pipeline->ncmds == 0) return 0;

  /*
   * 單一前景 built-in 直接在 parent Shell 執行。
   * cd、exit 需要修改 Shell 本身狀態，不能只在 child 中執行。
   */
  if (pipeline->ncmds == 1 && !pipeline->background) {
    Cmd* cmd = &pipeline->cmds[0];
    if (cmd->argc == 0) return 0;
    if (is_builtin(cmd->argv[0])) return exec_builtin(cmd);
  }

  /*
   * 外部指令、pipeline、背景執行都走 fork path。
   *
   * 對 n 段 Cmd，需要 n-1 條 pipe：
   *   pipes[i][0] 是讀端。
   *   pipes[i][1] 是寫端。
   *   cmd[i] stdout 接 pipes[i][1]。
   *   cmd[i+1] stdin 接 pipes[i][0]。
   */
  int npipes = pipeline->ncmds - 1;
  int pipes[MAX_PIPES][2];
  pid_t pids[MAX_PIPES];
  int nforked = 0;

  /* 先建立所有 pipe，後續 child 才能依序接線。 */
  for (int i = 0; i < npipes; i++) {
    if (pipe(pipes[i]) < 0) {
      perror("fwsh: pipe");
      /* pipe 建立到一半失敗時，只關閉已成功建立的 fd。 */
      for (int j = 0; j < i; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      return -1;
    }
  }

  /* 每一段非空 Cmd 都對應一個 child process。 */
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
       * child 接線規則：
       *   - 不是第一段：stdin 來自前一條 pipe 的讀端。
       *   - 不是最後段：stdout 接到目前 pipe 的寫端。
       *
       * 接好後必須關閉所有 pipe fd。
       * 否則讀端可能因為還有寫端被持有而等不到 EOF。
       */

      /* 非第一段：stdin 讀前一段輸出的資料。 */
      if (i > 0) dup2(pipes[i - 1][0], STDIN_FILENO);

      /* 非最後段：stdout 寫給下一段。 */
      if (i < npipes) dup2(pipes[i][1], STDOUT_FILENO);

      /* child 已完成 dup2，原始 pipe fd 不再需要。 */
      for (int j = 0; j < npipes; j++) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }

      /*
       * built-in 若出現在 pipeline 或 background 中，也在 child 執行。
       * 執行後用 _exit() 結束，避免 child 回到 parent 的控制流程。
       */
      if (is_builtin(cmd->argv[0])) _exit(exec_builtin(cmd));

      exec_external(cmd);
      _exit(127); /* 正常不會走到這裡，保留作為防禦。 */
    }

    /* parent 記錄 child pid 後，繼續建立下一段 Cmd。 */
    nforked++;
  }

  /*
   * parent 不讀寫 pipeline，只負責管理 child。
   * fork 完後關閉全部 pipe fd，避免影響 EOF。
   */
  for (int i = 0; i < npipes; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }

  /* 前景執行要等 child 結束；背景執行直接回到 prompt。 */
  int last_status = 0;

  if (!pipeline->background) {
    for (int i = 0; i < nforked; i++) {
      int status;
      waitpid(pids[i], &status, 0);
      /* Shell 慣例：pipeline 狀態以最後一段命令為主。 */
      if (i == nforked - 1)
        last_status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
  } else {
    /* 背景執行不等待，讓使用者可繼續輸入下一個命令。 */
    printf("[background]");
    for (int i = 0; i < nforked; i++) printf(" %d", pids[i]);
    printf("\n");
  }

  return last_status;
}
