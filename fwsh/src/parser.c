/*
 * parser.c — 命令列解析器
 *
 * 解析目標：將使用者輸入的原始字串轉換為 Pipeline 結構。
 *
 * 範例輸入：  cat firmware.bin | hexdump 0x100 > dump.txt &
 * 解析結果：
 *   Pipeline {
 *     ncmds = 2, background = 1,
 *     cmds[0] = { argv=["cat","firmware.bin"], in_file=NULL, out_file=NULL },
 *     cmds[1] = { argv=["hexdump","0x100"],   in_file=NULL, out_file="dump.txt"
 * }
 *   }
 *
 * 解析策略：
 *   採用「詞法分析（Lexer）+ 遞增建構」方式。
 *   Lexer 維護一個游標（pos）在輸入字串上滑動，
 *   遇到特殊字元時回傳對應 Token，遇到一般文字時讀取完整的「詞」。
 *
 * 引號處理：
 *   - 單引號：內容完全 literal，不做任何轉義。例：'hello world' → 一個詞。
 *   - 雙引號：支援 \" 和 \\ 轉義，其他字元 literal。
 *
 * 記憶體管理：
 *   所有字串以 strdup() 複製到 heap，free_pipeline() 負責全部釋放。
 */

#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Lexer 結構 ──────────────────────────────────── */

/*
 * Lexer 只是一個「帶游標的字串讀取器」。
 * 刻意不用全域變數，讓 parse_line 可重入（reentrant），
 * 這在多執行緒環境下很重要。
 */
typedef struct {
  const char* input; /* 指向原始輸入字串，不修改它 */
  int pos;           /* 當前讀取位置（byte index）  */
} Lexer;

static void lexer_init(Lexer* lex, const char* input) {
  lex->input = input;
  lex->pos = 0;
}

/* 跳過所有空白字元（空格、Tab） */
static void skip_whitespace(Lexer* lex) {
  while (lex->input[lex->pos] && isspace((unsigned char)lex->input[lex->pos]))
    lex->pos++;
}

/*
 * read_word - 讀取一個「詞」到 buf
 *
 * 停止條件：空白、|、<、>、&、\0
 * 特殊處理：引號（單引號/雙引號）內的內容整體視為一個詞的一部分。
 *
 * 回傳：讀取到的字元數
 */
static int read_word(Lexer* lex, char* buf, int bufsz) {
  int len = 0;
  char c;

  while ((c = lex->input[lex->pos]) != '\0') {
    /* 遇到未被引號包住的特殊字元，停止讀取 */
    if (isspace((unsigned char)c) || c == '|' || c == '<' || c == '>' ||
        c == '&')
      break;

    if (c == '\'') {
      /*
       * 單引號：消費開頭的 '，然後把所有內容直接複製，
       * 直到找到配對的結尾 '。
       * Shell 規範：單引號內不允許任何轉義。
       */
      lex->pos++; /* 消費開頭 ' */
      while ((c = lex->input[lex->pos]) != '\0' && c != '\'') {
        if (len + 1 < bufsz) buf[len++] = c;
        lex->pos++;
      }
      if (c == '\'') lex->pos++; /* 消費結尾 ' */

    } else if (c == '"') {
      /*
       * 雙引號：支援 \" 跳脫（允許在字串內含雙引號）
       *         和 \\ 跳脫（允許含反斜線）。
       * 其他 \x 組合保持原樣（不處理 $var 展開等進階功能）。
       */
      lex->pos++; /* 消費開頭 " */
      while ((c = lex->input[lex->pos]) != '\0' && c != '"') {
        if (c == '\\') {
          char next = lex->input[lex->pos + 1];
          if (next == '"' || next == '\\') {
            lex->pos++; /* 跳過反斜線 */
            c = lex->input[lex->pos];
          }
        }
        if (len + 1 < bufsz) buf[len++] = c;
        lex->pos++;
      }
      if (c == '"') lex->pos++; /* 消費結尾 " */

    } else {
      /* 普通字元，直接加入 */
      if (len + 1 < bufsz) buf[len++] = c;
      lex->pos++;
    }
  }

  buf[len] = '\0';
  return len;
}

/* ── 主解析函式 ──────────────────────────────────── */

int parse_line(const char* line, Pipeline* pipeline) {
  Lexer lex;
  char wordbuf[MAX_INPUT];
  int cmd_idx = 0;

  lexer_init(&lex, line);
  memset(pipeline, 0, sizeof(*pipeline));

  /* 初始化第一段 Cmd */
  Cmd* cur = &pipeline->cmds[0];
  memset(cur, 0, sizeof(*cur));

  /*
   * 主解析迴圈：每次迭代處理一個 Token。
   * Token 是 Lexer 從輸入字串中識別出的最小有意義單元。
   */
  while (1) {
    skip_whitespace(&lex);
    char c = lex.input[lex.pos];

    if (c == '\0' || c == '\n') {
      /* 輸入結束 */
      break;

    } else if (c == '|') {
      /*
       * 管線符號：目前 Cmd 結束，建立下一個。
       * 下一個 Cmd 的 stdin 會在 executor 中用 pipe() + dup2() 連接。
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
      /* 背景執行旗標 */
      lex.pos++;
      pipeline->background = 1;

    } else if (c == '<') {
      /*
       * 輸入重導向：< filename
       * 子行程在 executor 中會 open() 後 dup2() 到 STDIN_FILENO。
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
       * 輸出重導向：> filename 或 >> filename（追加模式）
       */
      lex.pos++;
      if (lex.input[lex.pos] == '>') {
        lex.pos++;
        cur->out_append = 1; /* >> 追加模式 */
      }
      skip_whitespace(&lex);
      read_word(&lex, wordbuf, sizeof(wordbuf));
      if (wordbuf[0] == '\0') {
        fprintf(stderr, "fwsh: missing filename after '>'\n");
        return -1;
      }
      cur->out_file = strdup(wordbuf);

    } else {
      /* 一般詞（指令名稱或引數），加入目前 Cmd 的 argv */
      read_word(&lex, wordbuf, sizeof(wordbuf));
      if (wordbuf[0] == '\0') continue; /* 不應發生，但防禦性判斷 */

      if (cur->argc >= MAX_ARGS - 1) {
        fprintf(stderr, "fwsh: too many arguments (max %d)\n", MAX_ARGS - 1);
        return -1;
      }
      cur->argv[cur->argc++] = strdup(wordbuf);
      cur->argv[cur->argc] = NULL; /* 維持 NULL 結尾，execvp 需要 */
    }
  }

  /*
   * 計算有效的 Cmd 數量。
   * 如果輸入以 | 結尾（例如 "ls | "），最後一段 argc == 0，
   * 此時應修正 ncmds 以避免執行空的 Cmd。
   */
  pipeline->ncmds = cmd_idx + 1;
  if (pipeline->ncmds > 1 && pipeline->cmds[pipeline->ncmds - 1].argc == 0)
    pipeline->ncmds--;

  return 0;
}

/* ── 記憶體釋放 ──────────────────────────────────── */

void free_pipeline(Pipeline* pipeline) {
  for (int i = 0; i < pipeline->ncmds; i++) {
    Cmd* cmd = &pipeline->cmds[i];

    /* 釋放所有 argv 字串 */
    for (int j = 0; j < cmd->argc; j++) {
      free(cmd->argv[j]);
      cmd->argv[j] = NULL;
    }

    /* 釋放重導向檔名 */
    free(cmd->in_file);
    free(cmd->out_file);
    cmd->in_file = NULL;
    cmd->out_file = NULL;
    cmd->argc = 0;
  }
  pipeline->ncmds = 0;
}
