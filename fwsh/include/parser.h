#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h - 命令列解析器介面
 *
 * parser 負責把使用者輸入的一行文字轉成 Pipeline。
 * 它只做語法解析，不負責 fork、exec 或檢查指令是否存在。
 *
 * 支援：
 *   - 單引號與雙引號，引號內的空白不切割 argv。
 *   - 管線符號 |
 *   - 輸入/輸出重導向 < > >>
 *   - 背景執行 &
 */

#include "shell.h"

/*
 * parse_line - 將一行輸入解析成 Pipeline
 *
 * @line     : 輸入字串；函式只讀取，不修改。
 * @pipeline : 解析結果；呼叫方提供結構本身。
 *
 * 回傳值：
 *   0  成功
 *  -1  語法錯誤或超出限制
 *
 * 注意：
 *   parse_line() 會用 strdup() 複製 argv 與重導向檔名。
 *   使用完 Pipeline 後一定要呼叫 free_pipeline()。
 */
int parse_line(const char* line, Pipeline* pipeline);

/*
 * free_pipeline - 釋放 Pipeline 內部配置的字串
 *
 * 只釋放 argv[]、in_file、out_file 指向的 heap 字串。
 * 不釋放 Pipeline 結構本身，因為它通常是 stack 區域變數。
 */
void free_pipeline(Pipeline* pipeline);

#endif /* PARSER_H */
