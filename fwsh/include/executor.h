#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * executor.h - Pipeline 執行介面
 *
 * executor 接收 parser 產生的 Pipeline，決定要直接執行
 * built-in，或 fork child 後執行外部指令。
 *
 * 主要處理：
 *   - 單一前景 built-in：在 Shell 行程內執行。
 *   - 外部指令：fork child，再用 execvp() 載入程式。
 *   - Pipeline：用 pipe() 與 dup2() 串接 stdin/stdout。
 *   - 重導向：用 open() 與 dup2() 改接 stdin/stdout。
 *   - 前景/背景：前景 waitpid()，背景交給 SIGCHLD 回收。
 */

#include "shell.h"

/*
 * execute_pipeline - 執行一個已解析完成的 Pipeline
 *
 * @pipeline : parse_line() 產生的 Pipeline。
 *
 * 回傳值：
 *   >= 0  最後一段前景指令的 exit status，或 built-in 回傳值。
 *   -1   pipe/fork 等系統呼叫失敗。
 *
 * 呼叫者仍負責在執行後呼叫 free_pipeline() 釋放 parser 配置的字串。
 */
int execute_pipeline(Pipeline* pipeline);

#endif /* EXECUTOR_H */
