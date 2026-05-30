#ifndef BUILTIN_H
#define BUILTIN_H

/*
 * builtin.h - 內建指令模組介面
 *
 * 內建指令由 fwsh 自己的 C 函式實作。
 * 和外部程式不同，單一前景內建指令可直接修改 Shell 狀態。
 *
 * 例：
 *   - cd 必須改變 Shell 本身的 cwd，因此不能只在 child 中執行。
 *   - exit 必須設定 g_shell.running，讓 REPL 停止。
 *
 * 本模組也放入常用的底層檢查工具：
 *   hexdump  以 hex + ASCII 顯示檔案內容。
 *   crc32    計算 IEEE 802.3 CRC-32。
 *   memmap   顯示 /proc/iomem 實體記憶體配置。
 */

#include "shell.h"

/*
 * is_builtin - 判斷指令名稱是否為 fwsh 內建指令
 *
 * 回傳 1 = 是，0 = 否
 */
int is_builtin(const char* name);

/*
 * exec_builtin - 執行 Cmd 對應的內建指令
 *
 * 回傳值沿用一般命令慣例：
 *   0     成功
 *   非 0  指令本身回報錯誤
 */
int exec_builtin(Cmd* cmd);

#endif /* BUILTIN_H */
