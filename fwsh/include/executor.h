#ifndef EXECUTOR_H
#define EXECUTOR_H

/*
 * executor.h — 指令執行器介面
 *
 * 負責接收已解析的 Pipeline，透過 fork()/exec() 機制執行。
 * 處理：
 *   - 單一指令的 fork/exec
 *   - 多段 pipeline 的 pipe() 串接
 *   - I/O 重導向（dup2 設定）
 *   - 前景 waitpid / 背景執行
 */

#include "shell.h"

/*
 * execute_pipeline - 執行完整的 Pipeline
 *
 * @pipeline : 已解析的 Pipeline 結構（唯讀）
 *
 * 回傳值：最後一段指令的結束碼（exit status），-1 表示系統呼叫失敗
 *
 * 執行順序：
 *   1. 若只有一段且為內建指令，直接呼叫內建函式（不 fork）
 *   2. 否則建立所有管線 pipe fd，對每段 fork 子行程
 *   3. 子行程中 dup2 設定標準 I/O，再 execvp
 *   4. 父行程關閉所有管線 fd，等待前景子行程結束
 */
int execute_pipeline(Pipeline* pipeline);

#endif /* EXECUTOR_H */
