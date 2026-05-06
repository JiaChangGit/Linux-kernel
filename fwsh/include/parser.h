#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h — 命令列解析器介面
 *
 * 負責將使用者輸入的原始字串解析為結構化的 Pipeline。
 * 支援：
 *   - 單引號與雙引號（引號內空白不切割）
 *   - 管線符號 |
 *   - 輸入/輸出重導向 < > >>
 *   - 背景執行 &
 */

#include "shell.h"

/*
 * parse_line - 解析一行輸入字串為 Pipeline 結構
 *
 * @line     : 輸入字串（唯讀）
 * @pipeline : 輸出的 Pipeline 結構（呼叫方負責配置記憶體）
 *
 * 回傳值：0 = 成功，-1 = 語法錯誤
 *
 * 注意：parse_line 會用 strdup() 複製所有字串，
 *       用完後必須呼叫 free_pipeline() 釋放記憶體。
 */
int parse_line(const char* line, Pipeline* pipeline);

/*
 * free_pipeline - 釋放 Pipeline 內所有動態配置的字串記憶體
 *
 * 只釋放 argv[]、in_file、out_file 等指向的堆積記憶體，
 * 不釋放 Pipeline 結構本身（因為通常是 stack 上的區域變數）。
 */
void free_pipeline(Pipeline* pipeline);

#endif /* PARSER_H */
