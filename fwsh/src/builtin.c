/*
 * builtin.c - fwsh 內建指令
 *
 * 一般 Shell 指令：
 *   cd, pwd, exit/quit, help, history, clear
 *
 * 底層檢查工具：
 *   hexdump  以 Hex + ASCII 顯示檔案內容。
 *   crc32    計算 CRC-32，檢查檔案內容是否改變。
 *   memmap   讀取 /proc/iomem，顯示實體記憶體配置。
 *
 * 指令分派方式：
 *   builtins[] 是名稱到函式的對照表。
 *   新增指令時，只要實作函式並在表中加一筆資料，
 *   不需要改 is_builtin() 或 exec_builtin() 的搜尋邏輯。
 */

#include "builtin.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "shell.h"

/* ── 內部函式前置宣告 ─────────────────────────────── */
static int builtin_cd(Cmd* cmd);
static int builtin_pwd(Cmd* cmd);
static int builtin_exit(Cmd* cmd);
static int builtin_help(Cmd* cmd);
static int builtin_history(Cmd* cmd);
static int builtin_clear(Cmd* cmd);
static int builtin_hexdump(Cmd* cmd);
static int builtin_crc32(Cmd* cmd);
static int builtin_memmap(Cmd* cmd);

/* ── 內建指令分派表 ───────────────────────────────── */
/*
 * 每筆資料保存：指令名稱、對應函式、help 顯示文字。
 * 最後一筆 name == NULL 是哨兵值，用來結束搜尋迴圈。
 */
typedef struct {
  const char* name;
  int (*func)(Cmd*);
  const char* desc;
} BuiltinEntry;

static BuiltinEntry builtins[] = {
    /* 一般 Shell 指令 */
    {"cd", builtin_cd,
     "cd <dir>              Change directory (支援 ~ 和 -)          "},
    {"pwd", builtin_pwd,
     "pwd                   Print working directory                  "},
    {"exit", builtin_exit,
     "exit [code]           Exit shell with optional status code     "},
    {"quit", builtin_exit,
     "quit                  Alias for exit                           "},
    {"help", builtin_help,
     "help                  Show this help message                   "},
    {"history", builtin_history,
     "history               Show command history                     "},
    {"clear", builtin_clear,
     "clear                 Clear terminal screen                    "},
    /* 底層檢查工具 */
    {"hexdump", builtin_hexdump,
     "hexdump <file> [len]  Hex+ASCII dump (supports 0x prefix)     "},
    {"crc32", builtin_crc32,
     "crc32 <file>          Compute CRC-32 checksum (IEEE 802.3)     "},
    {"memmap", builtin_memmap,
     "memmap                Show /proc/iomem physical memory layout  "},
    /* 哨兵：表示表格結尾。 */
    {NULL, NULL, NULL}};

/* ── 對外介面 ─────────────────────────────────────── */

int is_builtin(const char* name) {
  for (int i = 0; builtins[i].name; i++)
    if (strcmp(name, builtins[i].name) == 0) return 1;
  return 0;
}

int exec_builtin(Cmd* cmd) {
  for (int i = 0; builtins[i].name; i++)
    if (strcmp(cmd->argv[0], builtins[i].name) == 0)
      return builtins[i].func(cmd);
  return -1; /* 呼叫前通常已用 is_builtin() 檢查，正常不會到這裡。 */
}

/* ═══════════════════════════════════════════════════
 * 一般 Shell 指令
 * ═══════════════════════════════════════════════════ */

/*
 * cd - 切換 Shell 目前工作目錄
 *
 * 支援：
 *   cd         回到 HOME
 *   cd ~       回到 HOME
 *   cd -       切到上一個目錄，使用 OLDPWD
 *   cd <path>  切到指定路徑
 *
 * cd 必須在 parent Shell 行程中執行。
 * 如果在 child 中 chdir()，Shell 本身的 cwd 不會改變。
 */
static int builtin_cd(Cmd* cmd) {
  const char* dir;

  if (cmd->argc < 2 || strcmp(cmd->argv[1], "~") == 0) {
    dir = getenv("HOME");
    if (!dir) {
      fprintf(stderr, "cd: HOME environment variable not set\n");
      return 1;
    }
  } else if (strcmp(cmd->argv[1], "-") == 0) {
    dir = getenv("OLDPWD");
    if (!dir) {
      fprintf(stderr, "cd: OLDPWD not set (no previous directory)\n");
      return 1;
    }
    printf("%s\n", dir); /* cd - 依慣例會顯示切換後的路徑。 */
  } else {
    dir = cmd->argv[1];
  }

  /* 切換前先保存目前目錄，讓下一次 cd - 可回來。 */
  char old[512];
  if (getcwd(old, sizeof(old))) setenv("OLDPWD", old, 1);

  /* chdir() 會改變目前 process 的工作目錄。 */
  if (chdir(dir) != 0) {
    fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
    return 1;
  }
  return 0;
}

/* pwd - 顯示目前工作目錄。 */
static int builtin_pwd(Cmd* cmd) {
  (void)cmd; /* pwd 不需要讀取 argv。 */
  char cwd[512];
  if (getcwd(cwd, sizeof(cwd)))
    printf("%s\n", cwd);
  else
    perror("pwd");
  return 0;
}

/*
 * exit - 結束 fwsh
 *
 * 設定 g_shell.running = 0，shell_run() 下一輪檢查時會離開 REPL。
 * 可選引數是結束碼；目前用 atoi() 做簡單轉換。
 */
static int builtin_exit(Cmd* cmd) {
  int code = 0;
  if (cmd->argc >= 2) code = atoi(cmd->argv[1]);

  g_shell.running = 0;
  printf(COLOR_GREEN "Goodbye! fwsh exiting with code %d.\n" COLOR_RESET, code);
  return code;
}

/* help - 走訪 builtins[]，顯示所有內建指令。 */
static int builtin_help(Cmd* cmd) {
  (void)cmd;
  printf(COLOR_GREEN "fwsh v%s — Firmware Mini Shell\n" COLOR_RESET,
         FWSH_VERSION);
  printf("─────────────────────────────────────────────────────────────────\n");
  printf("  " COLOR_CYAN "General Commands\n" COLOR_RESET);
  for (int i = 0; builtins[i].name; i++) {
    if (strcmp(builtins[i].name, "hexdump") == 0)
      printf("\n  " COLOR_CYAN "Firmware Engineer Tools\n" COLOR_RESET);
    printf("  %s\n", builtins[i].desc);
  }
  printf("─────────────────────────────────────────────────────────────────\n");
  printf("  Supports: pipes (|), redirection (< > >>), background (&)\n");
  printf("  Tip: Use Ctrl+R to reverse-search history, Tab to autocomplete.\n");
  return 0;
}

/* history - 顯示 fwsh 自己保存的最近輸入命令。 */
static int builtin_history(Cmd* cmd) {
  (void)cmd;
  if (g_shell.hist_count == 0) {
    printf("  (no history)\n");
    return 0;
  }
  int start = g_shell.hist_head - g_shell.hist_count;
  for (int i = 0; i < g_shell.hist_count; i++) {
    int idx = (start + i) % MAX_HISTORY;
    printf("  %3d  %s\n", i + 1, g_shell.history[idx]);
  }
  return 0;
}

/* clear - 用 ANSI escape code 清除終端機畫面。 */
static int builtin_clear(Cmd* cmd) {
  (void)cmd;
  /*
   * \033[2J  清除整個畫面。
   * \033[H   將游標移到左上角。
   */
  printf("\033[2J\033[H");
  fflush(stdout);
  return 0;
}

/* ═══════════════════════════════════════════════════
 * 底層檢查工具
 * ═══════════════════════════════════════════════════ */

/*
 * builtin_hexdump - 以 Hex + ASCII 顯示檔案內容
 *
 * 用途：
 *   快速檢查二進位檔案內容，例如韌體映像、Flash dump、
 *   或其他不能直接用文字編輯器閱讀的資料。
 *
 * 顯示格式：每列 16 bytes。
 *   Offset  | 左半部 8 bytes | 右半部 8 bytes | ASCII 表示
 *
 * len 使用 strtol(base=0)，所以可輸入 128、0x80 等格式。
 */
static int builtin_hexdump(Cmd* cmd) {
  if (cmd->argc < 2) {
    fprintf(stderr, "Usage: hexdump <file> [max_bytes]\n");
    fprintf(stderr, "       max_bytes supports 0x prefix (e.g. 0x100 = 256)\n");
    return 1;
  }

  /* base=0 讓 strtol() 自動接受十進位、0x 十六進位與 0 開頭八進位。 */
  long max_bytes = 256;
  if (cmd->argc >= 3) {
    char* endptr;
    max_bytes = strtol(cmd->argv[2], &endptr, 0);
    if (max_bytes <= 0 || *endptr != '\0') {
      fprintf(stderr, "hexdump: invalid length '%s'\n", cmd->argv[2]);
      return 1;
    }
  }

  FILE* fp =
      fopen(cmd->argv[1], "rb"); /* rb 表示以二進位模式讀取。 */
  if (!fp) {
    fprintf(stderr, "hexdump: %s: %s\n", cmd->argv[1], strerror(errno));
    return 1;
  }

  printf(COLOR_GREEN "Hexdump: %s  (max %ld / 0x%lX bytes)\n" COLOR_RESET,
         cmd->argv[1], max_bytes, max_bytes);
  printf(
      "Offset    00 01 02 03 04 05 06 07  08 09 0A 0B 0C 0D 0E 0F"
      "  |ASCII           |\n");
  printf(
      "────────  ───────────────────────  ───────────────────────"
      "  ─────────────────\n");

  uint8_t buf[16];
  long offset = 0;
  size_t n;

  while (offset < max_bytes) {
    /* 每列最多顯示 16 bytes；最後一列可能不足 16 bytes。 */
    size_t to_read =
        (size_t)((max_bytes - offset) < 16 ? (max_bytes - offset) : 16);
    n = fread(buf, 1, to_read, fp);
    if (n == 0) break;

    /* 欄位 1：目前列的檔案偏移量。 */
    printf("%08lX  ", offset);

    /* 欄位 2：十六進位 byte，8 bytes 後多留一格方便閱讀。 */
    for (int i = 0; i < 16; i++) {
      if (i == 8) printf(" "); /* 左右各 8 bytes 分組。 */
      if (i < (int)n)
        printf("%02X ", buf[i]);
      else
        printf("   "); /* 不足 16 bytes 時補空白，維持欄位對齊。 */
    }

    /* 欄位 3：可列印字元直接顯示，不可列印字元用 '.'。 */
    printf(" |");
    for (size_t i = 0; i < n; i++)
      printf("%c", (buf[i] >= 0x20 && buf[i] < 0x7F) ? (char)buf[i] : '.');
    for (size_t i = n; i < 16; i++) printf(" ");
    printf("|\n");

    offset += (long)n;
  }

  fclose(fp);
  printf("────────\n");
  printf("Total: %ld bytes (0x%lX) displayed.\n", offset, offset);
  return 0;
}

/* ── CRC-32 查找表，使用 IEEE 802.3 反射多項式 ─────── */

/*
 * CRC-32 常用來檢查資料是否被改動。
 * 在韌體情境中，常見於映像檔驗證、OTA 更新檢查與通訊封包校驗。
 *
 * 這裡使用查找表法：
 *   先預算 0~255 每個 byte 的 CRC 貢獻。
 *   實際計算時，每個 byte 只需 XOR、查表與右移。
 */
static uint32_t crc32_table[256];
static int crc32_table_ready = 0;

static void crc32_build_table(void) {
  /*
   * 0xEDB88320 是 IEEE 802.3 CRC-32 的反射多項式。
   * 反射形式從最低位元開始處理，適合這個右移實作。
   */
  for (uint32_t byte = 0; byte < 256; byte++) {
    uint32_t crc = byte;
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 1)
        crc = 0xEDB88320U ^ (crc >> 1);
      else
        crc >>= 1;
    }
    crc32_table[byte] = crc;
  }
  crc32_table_ready = 1;
}

static int builtin_crc32(Cmd* cmd) {
  if (cmd->argc < 2) {
    fprintf(stderr, "Usage: crc32 <file>\n");
    return 1;
  }

  FILE* fp = fopen(cmd->argv[1], "rb");
  if (!fp) {
    fprintf(stderr, "crc32: %s: %s\n", cmd->argv[1], strerror(errno));
    return 1;
  }

  if (!crc32_table_ready) crc32_build_table();

  /*
   * 計算流程：
   *   1. 初始值設為 0xFFFFFFFF。
   *   2. 逐 byte 更新 CRC。
   *   3. 最後再 XOR 0xFFFFFFFF 取得輸出值。
   *
   * 初始與結尾都使用 0xFFFFFFFF，是常見 CRC-32 參數設定。
   */
  uint32_t crc = 0xFFFFFFFFU;
  uint8_t chunk[4096];
  size_t n;
  long total = 0;

  while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
    for (size_t i = 0; i < n; i++)
      crc = crc32_table[(crc ^ chunk[i]) & 0xFF] ^ (crc >> 8);
    total += (long)n;
  }
  fclose(fp);

  crc ^= 0xFFFFFFFFU; /* 產生最終 CRC-32 值。 */

  printf(COLOR_YELLOW "CRC32" COLOR_RESET "(%s) = " COLOR_GREEN
                      "0x%08X" COLOR_RESET "  (%ld bytes, %ld KB)\n",
         cmd->argv[1], crc, total, total / 1024);
  return 0;
}

/*
 * memmap - 顯示 Linux 實體記憶體配置
 *
 * /proc/iomem 由 Linux kernel 提供，列出實體位址空間如何分配。
 * 可用來觀察 System RAM、Kernel、initrd、ACPI、PCI 等區域。
 *
 * 注意：
 *   在 WSL、容器或虛擬化環境中，位址可能被遮蔽或簡化。
 */
static int builtin_memmap(Cmd* cmd) {
  (void)cmd;

  FILE* fp = fopen("/proc/iomem", "r");
  if (!fp) {
    fprintf(stderr, "memmap: cannot open /proc/iomem: %s\n", strerror(errno));
    fprintf(stderr,
            "       (This file requires Linux kernel; "
            "may not exist in containers)\n");
    return 1;
  }

  printf(COLOR_GREEN "Physical Memory Map (/proc/iomem)\n" COLOR_RESET);
  printf("─────────────────────────────────────────────────────\n");
  printf(COLOR_YELLOW "  Yellow" COLOR_RESET " = System RAM  " COLOR_CYAN
                      "  Cyan" COLOR_RESET " = Kernel  " COLOR_MAGENTA
                      " Magenta" COLOR_RESET " = ACPI/PCI\n");
  printf("─────────────────────────────────────────────────────\n");

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    /* 用關鍵字簡單分類，讓重要區域較容易掃讀。 */
    if (strstr(line, "System RAM"))
      printf(COLOR_YELLOW "%s" COLOR_RESET, line);
    else if (strstr(line, "Kernel") || strstr(line, "kernel") ||
             strstr(line, "initrd"))
      printf(COLOR_CYAN "%s" COLOR_RESET, line);
    else if (strstr(line, "ACPI") || strstr(line, "PCI") ||
             strstr(line, "Reserved"))
      printf(COLOR_MAGENTA "%s" COLOR_RESET, line);
    else
      printf("%s", line);
  }

  fclose(fp);
  return 0;
}
