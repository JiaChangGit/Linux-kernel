/*
 * parser.c - 命令列解析器
 *
 * 目的：
 *   將使用者輸入的一行文字轉成 Pipeline 結構。
 *   parser 只處理語法，不負責執行指令。
 *
 * 範例：
 *   cat firmware.bin | hexdump 0x100 > dump.txt &
 *
 * 解析成：
 *   - Pipeline.ncmds = 2
 *   - Pipeline.background = 1
 *   - cmds[0].argv = ["cat", "firmware.bin", NULL]
 *   - cmds[1].argv = ["hexdump", "0x100", NULL]
 *   - cmds[1].out_file = "dump.txt"
 *
 * 解析策略：
 *   用 Lexer 保存目前讀取位置，從左到右掃描字串。
 *   遇到一般文字就讀成 word；遇到 |、<、>、& 就更新 Pipeline。
 *
 * 引號規則：
 *   - 單引號：內容原樣保留，不處理跳脫。
 *   - 雙引號：支援 \" 和 \\，其他字元原樣保留。
 *
 * 記憶體管理：
 *   parser 會用 strdup() 複製 argv 與檔名。
 *   呼叫端必須用 free_pipeline() 釋放。
 */

#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Lexer 狀態 ──────────────────────────────────── */

/*
 * Lexer 是帶有游標的唯讀字串讀取器。
 * input 指向原始命令列，pos 表示目前讀到哪個 byte。
 * 狀態放在區域變數中，避免 parser 依賴全域游標。
 */
typedef struct {
  const char* input; /* 原始輸入字串；parser 不修改內容 */
  int pos;           /* 目前讀取位置，以 byte 為單位 */
} Lexer;

static void lexer_init(Lexer* lex, const char* input) {
  lex->input = input;
  lex->pos = 0;
}

/* 跳過空白字元，讓主迴圈直接看下一個有效符號。 */
static void skip_whitespace(Lexer* lex) {
  while (lex->input[lex->pos] && isspace((unsigned char)lex->input[lex->pos]))
    lex->pos++;
}

/*
 * read_word - 從目前位置讀出一個 word
 *
 * 停止條件：
 *   未被引號包住的空白、|、<、>、& 或字串結尾。
 *
 * 引號內的空白會保留在同一個 word。
 * 回傳值是寫入 buf 的字元數。
 */
static int read_word(Lexer* lex, char* buf, int bufsz) {
  int len = 0;
  char c;

  while ((c = lex->input[lex->pos]) != '\0') {
    /* 未被引號包住的特殊字元由主解析迴圈處理。 */
    if (isspace((unsigned char)c) || c == '|' || c == '<' || c == '>' ||
        c == '&')
      break;

    if (c == '\'') {
      /*
       * 單引號內全部視為一般文字。
       * 例：'a b' 會形成同一個 argv，不會被空白切開。
       */
      lex->pos++; /* 跳過開頭的 ' */
      while ((c = lex->input[lex->pos]) != '\0' && c != '\'') {
        if (len + 1 < bufsz) buf[len++] = c;
        lex->pos++;
      }
      if (c == '\'') lex->pos++; /* 跳過結尾的 ' */

    } else if (c == '"') {
      /*
       * 雙引號也會保留空白。
       * 目前只處理 \" 與 \\，不做 $VAR 展開或命令替換。
       */
      lex->pos++; /* 跳過開頭的 " */
      while ((c = lex->input[lex->pos]) != '\0' && c != '"') {
        if (c == '\\') {
          char next = lex->input[lex->pos + 1];
          if (next == '"' || next == '\\') {
            lex->pos++; /* 跳過跳脫用的反斜線 */
            c = lex->input[lex->pos];
          }
        }
        if (len + 1 < bufsz) buf[len++] = c;
        lex->pos++;
      }
      if (c == '"') lex->pos++; /* 跳過結尾的 " */

    } else {
      /* 一般字元直接加入目前 word。 */
      if (len + 1 < bufsz) buf[len++] = c;
      lex->pos++;
    }
  }

  buf[len] = '\0';
  return len;
}

/* ── 主解析流程 ──────────────────────────────────── */

int parse_line(const char* line, Pipeline* pipeline) {
  Lexer lex;
  char wordbuf[MAX_INPUT];
  int cmd_idx = 0;

  lexer_init(&lex, line);
  memset(pipeline, 0, sizeof(*pipeline));

  /* 一開始至少有第一段 Cmd。 */
  Cmd* cur = &pipeline->cmds[0];
  memset(cur, 0, sizeof(*cur));

  /*
   * 主迴圈每次處理一個語法單元：
   *   word 加到 argv。
   *   | 建立下一段 Cmd。
   *   <、>、>> 記錄重導向檔名。
   *   & 標記背景執行。
   */
  while (1) {
    skip_whitespace(&lex);
    char c = lex.input[lex.pos];

    if (c == '\0' || c == '\n') {
      /* 讀到字串結尾，解析完成。 */
      break;

    } else if (c == '|') {
      /*
       * | 代表目前 Cmd 結束。
       * executor 會用 pipe() 把前一段 stdout 接到下一段 stdin。
       */
      lex.pos++;
      cmd_idx++;
      if (cmd_idx >= MAX_PIPES) {
        fprintf(stderr, "fwsh: too many pipes (max %d)\n", MAX_PIPES);
        return -1;
      }
      cur = &pipeline->cmds[cmd_idx];
      memset(cur, 0, sizeof(*cur));

    } else if (c == '&') {
      /* & 只標記整個 Pipeline 背景執行。 */
      lex.pos++;
      pipeline->background = 1;

    } else if (c == '<') {
      /*
       * < filename：
       * parser 只保存檔名，真正 open() 與 dup2() 在 executor child 內做。
       */
      lex.pos++;
      skip_whitespace(&lex);
      read_word(&lex, wordbuf, sizeof(wordbuf));
      if (wordbuf[0] == '\0') {
        fprintf(stderr, "fwsh: missing filename after '<'\n");
        return -1;
      }
      cur->in_file = strdup(wordbuf);

    } else if (c == '>') {
      /*
       * > filename 或 >> filename：
       * 只記錄檔名與是否追加，實際開檔由 executor 處理。
       */
      lex.pos++;
      if (lex.input[lex.pos] == '>') {
        lex.pos++;
        cur->out_append = 1; /* >> 代表追加到檔案尾端。 */
      }
      skip_whitespace(&lex);
      read_word(&lex, wordbuf, sizeof(wordbuf));
      if (wordbuf[0] == '\0') {
        fprintf(stderr, "fwsh: missing filename after '>'\n");
        return -1;
      }
      cur->out_file = strdup(wordbuf);

    } else {
      /* 一般 word 可能是指令名稱，也可能是參數。 */
      read_word(&lex, wordbuf, sizeof(wordbuf));
      if (wordbuf[0] == '\0') continue; /* 防止空 word 進入 argv。 */

      if (cur->argc >= MAX_ARGS - 1) {
        fprintf(stderr, "fwsh: too many arguments (max %d)\n", MAX_ARGS - 1);
        return -1;
      }
      cur->argv[cur->argc++] = strdup(wordbuf);
      cur->argv[cur->argc] = NULL; /* execvp() 需要 NULL 結尾的 argv。 */
    }
  }

  /*
   * 設定有效 Cmd 數量。
   * 若使用者輸入以 | 結尾，例如 "ls | "，最後一段沒有 argv，
   * 這裡會略過空 Cmd，避免 executor 嘗試執行空命令。
   */
  pipeline->ncmds = cmd_idx + 1;
  if (pipeline->ncmds > 1 && pipeline->cmds[pipeline->ncmds - 1].argc == 0)
    pipeline->ncmds--;

  return 0;
}

/* ── Pipeline 記憶體釋放 ──────────────────────────── */

void free_pipeline(Pipeline* pipeline) {
  for (int i = 0; i < pipeline->ncmds; i++) {
    Cmd* cmd = &pipeline->cmds[i];

    /* argv 中每個字串都由 parse_line() 的 strdup() 建立。 */
    for (int j = 0; j < cmd->argc; j++) {
      free(cmd->argv[j]);
      cmd->argv[j] = NULL;
    }

    /* in_file/out_file 也由 parser 配置，這裡一併釋放。 */
    free(cmd->in_file);
    free(cmd->out_file);
    cmd->in_file = NULL;
    cmd->out_file = NULL;
    cmd->argc = 0;
  }
  pipeline->ncmds = 0;
}
