#ifndef BUILTIN_H
#define BUILTIN_H

/*
 * builtin.h — 內建指令模組介面
 *
 * 「內建指令」與普通外部程式的最大差異在於：
 *   - 內建指令在 Shell 行程本身執行（不 fork）
 *   - 因此可以直接修改 Shell 的狀態，例如 cd 更改工作目錄
 *   - 外部程式在子行程中執行，改變 cwd 不影響 Shell 本身
 *
 * 韌體工程師專屬工具：
 *   hexdump  - 以 hex + ASCII 格式顯示檔案內容（除錯韌體映像檔）
 *   crc32    - 計算 CRC-32 檢查碼（驗證韌體完整性）
 *   memmap   - 顯示 /proc/iomem 實體記憶體佈局
 */

#include "shell.h"

/*
 * is_builtin - 判斷指令名稱是否為內建指令
 * 回傳 1 = 是，0 = 否
 */
int is_builtin(const char* name);

/*
 * exec_builtin - 執行對應的內建指令
 * 回傳指令結束碼（0 = 成功，非 0 = 錯誤）
 */
int exec_builtin(Cmd* cmd);

#endif /* BUILTIN_H */
