/*
 * main.c — fwsh 程式入口點
 *
 * 刻意保持極簡：入口點只負責 init → run → cleanup 的生命週期，
 * 所有實際邏輯分散在各子模組，符合「單一責任原則」。
 *
 * 編譯：gcc -Wall -Iinclude src/*.c -o fwsh -lreadline
 */

#include "shell.h"

int main(void) {
  shell_init();    /* 初始化訊號處理、印出 Banner         */
  shell_run();     /* 進入 REPL 主迴圈（Read-Eval-Print） */
  shell_cleanup(); /* 釋放所有動態記憶體                  */
  return 0;
}
