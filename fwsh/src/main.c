/*
 * main.c - fwsh 程式入口
 *
 * main() 只負責 Shell 的三段生命週期：
 *   1. shell_init()     初始化訊號處理、Readline 與全域狀態。
 *   2. shell_run()      進入互動式 REPL 主迴圈。
 *   3. shell_cleanup()  離開前釋放 Shell 保存的資源。
 *
 * 實際解析、執行與內建指令邏輯分散在各模組，入口點保持單純。
 */

#include "shell.h"

int main(void) {
  shell_init();    /* 初始化 Shell 執行環境 */
  shell_run();     /* 進入 Read-Eval-Print Loop */
  shell_cleanup(); /* 釋放 Shell 全域資源 */
  return 0;
}
